#include "avi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#define READ_CHUNK (16u * 1024u)
/* Conservative estimate, not a guaranteed worst-case transfer speed. */
#define PREFETCH_BYTES_PER_MS 600u
#define PREFETCH_GUARD_MS 8u
#define FCC(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
/* Read a little-endian 32-bit value from an unaligned byte array. */
static uint32_t rd32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/* Read a little-endian 16-bit value from an unaligned byte array. */
static uint16_t rd16(const unsigned char *p){return (uint16_t)(p[0]|(p[1]<<8));}
/* Read one RIFF chunk identifier and payload length from the file. */
static int hdr(FILE *f,uint32_t *id,uint32_t *sz){unsigned char b[8];if(fread(b,1,8,f)!=8)return 0;*id=rd32(b);*sz=rd32(b+4);return 1;}
/* Compute the payload end offset, rejecting lengths above the supported signed range. */
static long endpos(long start,uint32_t size){if(size>0x7fffffffU)return -1;return start+(long)size;}
/* Inspect stream headers and formats to discover supported MJPEG and PCM streams. */
static void parse_strl(AVI *a,long end){
 FILE *f=a->f;uint32_t stream_type=0,stream_handler=0,stream_num=0;uint32_t rate=0,scale=0;
 /* Determine stream index from caller, supplied via audio/video IDs later. */
 long pos=ftell(f);unsigned char b[64];
 while(pos+8<=end){uint32_t id,n;if(!hdr(f,&id,&n))break;long data=ftell(f),next=endpos(data,n);if(next<0||next>end)break;
 if(id==FCC('s','t','r','h')&&n>=48){size_t r=n<sizeof(b)?n:sizeof(b);if(fread(b,1,r,f)==r){stream_type=rd32(b);stream_handler=rd32(b+4);scale=rd32(b+20);rate=rd32(b+24);}}
 if(id==FCC('s','t','r','f')){
  if(stream_type==FCC('v','i','d','s')&&stream_handler==FCC('M','J','P','G')&&n>=40){
   if(fread(b,1,40,f)==40 && rd32(b+16)==FCC('M','J','P','G')){
    a->width=rd32(b+4);a->height=rd32(b+8);a->fps_num=rate;a->fps_den=scale;a->video_id=stream_num;a->has_video=1;
   }
  }else if(stream_type==FCC('a','u','d','s')&&n>=16){
   if(fread(b,1,16,f)==16 && rd16(b)==1){a->audio_channels=rd16(b+2);a->audio_rate=rd32(b+4);a->audio_bits=rd16(b+14);a->audio_id=stream_num;a->has_audio=1;}
  }
 }
 fseek(f,next+(n&1),SEEK_SET);pos=ftell(f);
 }
}
/* Open a read-only AVI, parse its stream metadata, validate the profile and allocate its sequential cache. */
int avi_open(AVI *a,const char *path,char *err,size_t errlen){
 memset(a,0,sizeof(*a));snprintf(err,errlen,"Invalid AVI");a->f=fopen(path,"rb");if(!a->f){snprintf(err,errlen,"Cannot open AVI");return 0;}
 FILE *f=a->f;unsigned char riff[12];if(fread(riff,1,12,f)!=12||rd32(riff)!=FCC('R','I','F','F')||rd32(riff+8)!=FCC('A','V','I',' '))goto fail;
 long fileend=endpos(8,rd32(riff+4));if(fileend<12)goto fail;
 if(fseek(f,0,SEEK_END))goto fail;
 long actual_end=ftell(f);
 if(actual_end<fileend)goto fail;
 a->riff_end=fileend;a->file_end=actual_end;
 if(fseek(f,12,SEEK_SET))goto fail;
 long pos=12;uint32_t stream_idx=0;
 while(pos+12<=fileend){uint32_t id,n;if(!hdr(f,&id,&n))break;long data=ftell(f),next=endpos(data,n);if(next<0||next>fileend)break;
 if(id==FCC('L','I','S','T')&&n>=4){unsigned char typ[4];if(fread(typ,1,4,f)!=4)break;uint32_t t=rd32(typ);
  if(t==FCC('h','d','r','l')){long hpos=ftell(f);while(hpos+12<=next){uint32_t sub,sz;if(!hdr(f,&sub,&sz))break;long subdata=ftell(f),subnext=endpos(subdata,sz);if(subnext<0||subnext>next)break;
    if(sub==FCC('L','I','S','T')&&sz>=4){unsigned char subtyp[4];if(fread(subtyp,1,4,f)!=4)break;if(rd32(subtyp)==FCC('s','t','r','l')){int had_video=a->has_video,had_audio=a->has_audio;parse_strl(a,subnext);/* Assign IDs only when each stream is first discovered. */if(!had_video&&a->has_video)a->video_id=stream_idx;if(!had_audio&&a->has_audio)a->audio_id=stream_idx;stream_idx++;}}
    fseek(f,subnext+(sz&1),SEEK_SET);hpos=ftell(f);
   }}
  if(t==FCC('m','o','v','i')){a->movi_start=data+4;a->movi_end=next;}
 }
 fseek(f,next+(n&1),SEEK_SET);pos=ftell(f);
 }
 if(!a->has_video||!a->movi_start||a->width!=320||a->height<2||a->height>240||a->height%2||a->fps_num==0||a->fps_den==0){snprintf(err,errlen,"Unsupported video profile");goto fail;}
 if(a->has_audio&&(a->audio_rate!=32000||a->audio_bits!=16||(a->audio_channels!=1&&a->audio_channels!=2))){snprintf(err,errlen,"Unsupported PCM profile");goto fail;}
 /* The header parser above retains its original stdio operations.
    Install the RAM cache only for sequential movi playback. */
 a->read_ahead=memalign(32,AVI_READ_AHEAD);
 if(!a->read_ahead){snprintf(err,errlen,"Cannot allocate AVI read buffer");goto fail;}
 a->cache_start=0;a->cache_length=0;a->cache_consumed=0;
 a->cursor=a->movi_start;return 1;
fail:avi_close(a);return 0;
}
/* All byte ranges are absolute offsets in the AVI file. The cache contains
   [cache_start, cache_start + cache_length). cache_consumed records the highest
   offset already delivered to the caller, so those bytes can be discarded.
   Only this single thread touches the FILE* and the cache. */
/* Discard consumed cache bytes while preserving unread bytes and their absolute file offset. */
static void avi_compact(AVI *a) {
 size_t drop=a->cache_consumed;
 if(drop==0)return;
 size_t keep=a->cache_length-drop;
 if(keep)memmove(a->read_ahead,a->read_ahead+drop,keep);
 a->cache_start+=(long)drop;
 a->cache_length=keep;
 a->cache_consumed=0;
}
/* Reset the input cache to an empty window at the requested absolute file offset. */
static void avi_reset_window(AVI *a,long pos) {
 a->cache_start=pos;
 a->cache_length=0;
 a->cache_consumed=0;
}
/* Counts time spent seeking AND reading; reported separately from time spent
   scanning AVI chunks and copying data into jpegdata. */
/* Read a bounded span into the cache and record refill timing and byte counts. */
static size_t avi_fill(AVI *a,long pos,size_t count,int prefetch) {
 uint64_t t0=gettime();
 if(fseek(a->f,pos,SEEK_SET))return 0;
 size_t got=fread(a->read_ahead+a->cache_length,1,count,a->f);
 uint64_t elapsed=diff_ticks(t0,gettime());
 a->refill_calls++;
 a->refill_bytes+=(uint64_t)got;
 if(elapsed>a->refill_ticks_max)a->refill_ticks_max=elapsed;
 if(prefetch && got)a->prefetch_calls++;
 if(got)a->cache_length+=got;
 return got;
}
/* Serve an absolute byte range from the cache, refilling as necessary within the current movi region. */
static int avi_cached_read(AVI *a,long pos,void *dest,size_t n) {
 unsigned char *out=(unsigned char *)dest;
 if(!a || !a->f || !a->read_ahead)return 0;
 /* Never let an invalid chunk make a read run beyond the movi region. */
 if(pos<a->movi_start || pos>a->movi_end || n>(size_t)(a->movi_end-pos))return 0;
 while(n) {
  long end=a->cache_start+(long)a->cache_length;
  if(pos>=a->cache_start && pos<end) {
   size_t off=(size_t)(pos-a->cache_start);
   size_t take=a->cache_length-off;
   if(take>n)take=n;
   memcpy(out,a->read_ahead+off,take);
   out+=take;n-=take;pos+=(long)take;
   if(off+take>a->cache_consumed)a->cache_consumed=off+take;
   continue;
  }
  /* A skipped AVI chunk or a backward access requires a new window.
     Contiguous access at the end of the cache can append instead. */
  if(pos!=end)avi_reset_window(a,pos);
  if(a->cache_length>=AVI_READ_AHEAD) {
   avi_compact(a);
   if(a->cache_length>=AVI_READ_AHEAD)avi_reset_window(a,pos);
  }
  end=a->cache_start+(long)a->cache_length;
  if(end!=pos)avi_reset_window(a,pos);
  size_t space=AVI_READ_AHEAD-a->cache_length;
  size_t left=(size_t)(a->movi_end-pos);
  size_t want=READ_CHUNK;
  if(want>space)want=space;
  if(want>left)want=left;
  if(!want || !avi_fill(a,pos,want,0))return 0;
 }
 return 1;
}
/* Best-effort, synchronous prefetch during spare CPU time. 'available_ms'
   is the measured time until the next presentation target. SD read latency
   varies, so an estimate cannot guarantee a read completes by the deadline.
   A video frame >128 KiB still requires synchronous reads while decoding. */
/* Perform optional synchronous cache read-ahead within the estimated remaining presentation time. */
int avi_prefetch(AVI *a,uint32_t available_ms) {
 if(!a || !a->f || !a->read_ahead)return 0;
 if(available_ms<=PREFETCH_GUARD_MS+7u)return 0;
 size_t budget=(size_t)(available_ms-PREFETCH_GUARD_MS)*PREFETCH_BYTES_PER_MS;
 if(budget>READ_CHUNK)budget=READ_CHUNK;
 budget&=~(size_t)511; /* Prefer complete SD sectors. */
 if(budget<4096)return 0;
 /* Playback has already copied the current JPEG into its own buffer.
    If the parser jumped forward past audio, do not prefetch the gap. */
 long end=a->cache_start+(long)a->cache_length;
 if(a->cursor>end)avi_reset_window(a,a->cursor);
 if(AVI_READ_AHEAD-a->cache_length<budget && a->cache_consumed)avi_compact(a);
 size_t space=AVI_READ_AHEAD-a->cache_length;
 if(budget>space)budget=space;
 long file_pos=a->cache_start+(long)a->cache_length;
 if(file_pos>=a->movi_end)return 0;
 size_t left=(size_t)(a->movi_end-file_pos);
 if(budget>left)budget=left;
 if(budget<4096)return 0;
 return (int)avi_fill(a,file_pos,budget,1);
}
/* Move only between validated RIFF chunks; never scan video bytes for AVIX.
   Skip index chunks following movi by seeking to the enclosing RIFF end.
   This is sequential playback, not indexed seeking. */
/* Locate the next sequential AVIX segment and reset the cursor to its validated movi list. */
static int avi_advance_avix(AVI *a) {
 FILE *f=a->f;
 long start=a->riff_end+(a->riff_end&1);
 if(start<0 || start>a->file_end || a->file_end-start<12)return 0;
 unsigned char r[12];
 if(fseek(f,start,SEEK_SET) || fread(r,1,12,f)!=12)return 0;
 if(rd32(r)!=FCC('R','I','F','F') || rd32(r+8)!=FCC('A','V','I','X'))return 0;
 long end=endpos(start+8,rd32(r+4));
 if(end<start+12 || end>a->file_end)return 0;
 long pos=start+12;
 while(pos>=start+12 && end-pos>=12){
  uint32_t id,n;
  if(fseek(f,pos,SEEK_SET)||!hdr(f,&id,&n))return 0;
  long data=ftell(f),next=endpos(data,n);
  if(next<0||next>end)return 0;
  if(id==FCC('L','I','S','T') && n>=4){
   unsigned char b[4];
   if(fread(b,1,4,f)!=4)return 0;
   if(rd32(b)==FCC('m','o','v','i')){
    a->riff_end=end;
    a->movi_start=data+4;
    a->movi_end=next;
    a->cursor=a->movi_start;
    avi_reset_window(a,a->cursor);
    return 1;
   }
  }
  long after=next+(n&1);
  if(after<=pos||after>end)return 0;
  pos=after;
 }
 return 0;
}
/* Return the next AVI chunk, following supported rec lists and sequential AVIX segments. */
int avi_next(AVI *a,AVIChunk *out){
 while(a->cursor+8<=a->movi_end){
  unsigned char b[8];
  if(!avi_cached_read(a,a->cursor,b,sizeof(b))){a->read_error=1;return 0;}
  uint32_t id=rd32(b),n=rd32(b+4);
  long data=a->cursor+8,next=endpos(data,n);
  if(next<0||next>a->movi_end){a->read_error=1;return 0;}
  a->cursor=next+(n&1);
  if(id==FCC('L','I','S','T')&&n>=4){
   unsigned char t[4];
   if(!avi_cached_read(a,data,t,sizeof(t))){a->read_error=1;return 0;}
   if(rd32(t)==FCC('r','e','c',' ')){
    a->cursor=data+4;
    continue;
   }
  }
  out->id=id;out->size=n;out->data_pos=data;return 1;
 }
 if(avi_advance_avix(a))return avi_next(a,out);
 if(ferror(a->f) || a->file_end-(a->riff_end+(a->riff_end&1))>=12)a->read_error=1;
 return 0;
}
/* Read an entire chunk into a caller-supplied buffer, respecting its capacity. */
int avi_read(AVI *a,const AVIChunk *c,void *dest,size_t cap){
 if(c->size>cap)return 0;
 return avi_cached_read(a,c->data_pos,dest,c->size);
}
/* Read a validated subsection of an AVI chunk without allocating a whole-chunk buffer. */
int avi_read_part(AVI *a,const AVIChunk *c,size_t offset,void *dest,size_t n){
 if(offset>c->size || n>(size_t)c->size-offset)return 0;
 return avi_cached_read(a,c->data_pos+(long)offset,dest,n);
}
/* Close the file, free its cache and reset the reader state. */
void avi_close(AVI *a){
 if(a->f)fclose(a->f);
 free(a->read_ahead);
 memset(a,0,sizeof(*a));
}
