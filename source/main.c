/* BEANZ AVI Player v1.3 - GameCube libogc / libjpeg playback engine. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include "frame_queue.h"
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <setjmp.h>
#include <jpeglib.h>
#include "avi.h"
#include "pcm_audio.h"
#define WIDTH 320
#define MAXHEIGHT 240
#define MAXJPEG (1024*1024)
#define MAXFILES 128
#define PATHCAP 512
#define FCC(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
static GXRModeObj *vmode;
static void *xfb[2];static int fb=0;
static uint8_t *linear,*tiled,*jpegdata;
static uint8_t *fifo;
/* Small permanent scratch buffer; do not add 8 KiB to the GameCube stack. */
static uint8_t pcm_scratch[PCM_RAW_SCRATCH];
static int ready=0,playing=0,paused=0;
static uint32_t frames=0;
static char status[256]="Ready";
static char debug_status[512];
static char folder[PATHCAP]="sd:/";
/* Keep browser navigation within the volume selected at startup. */
static char mount_root[16]="sd:/";
static struct {char name[256];int dir;} entries[MAXFILES];
static int count=0,selected=0,scroll=0;
static const char *mounted_name = NULL;
/* Convert four characters into the little-endian AVI FourCC representation. */
static uint32_t read32(const char *s){return FCC(s[0],s[1],s[2],s[3]);}
/* Probe a storage interface and mount its FAT filesystem under the given volume name. */
static int mount_sd(const char *label, const char *name,
                    const DISC_INTERFACE *device) {
 printf("%s: ", label);
 if (!device->startup()) {
  printf("startup failed\n");
  return 0;
 }
 if (!device->isInserted()) {
  printf("card not detected\n");
  return 0;
 }
 if (!fatMountSimple(name, device)) {
  printf("FAT mount failed\n");
  return 0;
 }
 printf("mounted OK\n");
 return 1;
}
/* Initialize video, allocate both framebuffers, and attach the browser console. */
static void setup_console(void){
 VIDEO_Init();vmode=VIDEO_GetPreferredMode(NULL);
 xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
 VIDEO_Configure(vmode);VIDEO_SetNextFramebuffer(xfb[0]);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 console_init(xfb[0],20,20,vmode->fbWidth-20,vmode->xfbHeight-20,vmode->fbWidth*VI_DISPLAY_PIX_SZ);
 fb=1; /* Offscreen XFB for the first browser redraw. */
}
/* Configure GX for textured video quads and framebuffer copies in the active video mode. */
static void init_gx(void){
 GX_Init(fifo,256*1024);GX_SetCopyClear((GXColor){0,0,0,255},0x00ffffff);
 GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
 GX_SetDispCopyYScale((f32)vmode->xfbHeight/(f32)vmode->efbHeight);
 GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
 GX_SetDispCopySrc(0,0,vmode->fbWidth,vmode->efbHeight);
 GX_SetDispCopyDst(vmode->fbWidth,vmode->xfbHeight);
 GX_SetCopyFilter(vmode->aa,vmode->sample_pattern,GX_TRUE,vmode->vfilter);
 GX_SetFieldMode(vmode->field_rendering,((vmode->viHeight==2*vmode->xfbHeight)?GX_ENABLE:GX_DISABLE));
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);
 GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
 GX_SetNumChans(0);GX_SetNumTexGens(1);
 GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
 GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XY,GX_F32,0);
 GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
 GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 Mtx44 projection;guOrtho(projection,0,480,0,640,0,1);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 Mtx model;guMtxIdentity(model);GX_LoadPosMtxImm(model,GX_PNMTX0);
}
/* Render a prepared RGB565 texture, complete the GX copy, and present at retrace before releasing its slot. */
static void show_frame_from(int height,const uint8_t *texture){
 int padded=(height+3)&~3;
 GXTexObj tex;GX_InitTexObj(&tex,(void *)texture,WIDTH,padded,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&tex,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
 GX_InvalidateTexAll();GX_LoadTexObj(&tex,GX_TEXMAP0);
 float drawh=(float)height*2.0f;float y=(480.0f-drawh)*0.5f;
 float t=(float)height/(float)padded;
 GX_Begin(GX_QUADS,GX_VTXFMT0,4);
 GX_Position2f32(0,y);GX_TexCoord2f32(0,0);
 GX_Position2f32(640,y);GX_TexCoord2f32(1,0);
 GX_Position2f32(640,y+drawh);GX_TexCoord2f32(1,t);
 GX_Position2f32(0,y+drawh);GX_TexCoord2f32(0,t);
 GX_End();
 /* Complete both the draw and its EFB-to-XFB copy before VI receives it. */
 GX_CopyDisp(xfb[fb],GX_TRUE);GX_DrawDone();
 VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();fb^=1;
}
/* Present the shared scratch texture used by the video-only playback path. */
static void show_frame(int height){show_frame_from(height,tiled);}
/* Convert linear RGB565 into GX 4x4 tiled layout and flush the destination cache before publication. */
static void swizzle_to(int height,uint8_t *tiled){
 int ph=(height+3)&~3;memset(tiled,0,WIDTH*ph*2);
 for(int y=0;y<height;y++)for(int x=0;x<WIDTH;x++){
  size_t o=((size_t)(y>>2)*(WIDTH>>2)+(x>>2))*32+((y&3)*4+(x&3))*2;
  tiled[o]=linear[(y*WIDTH+x)*2];tiled[o+1]=linear[(y*WIDTH+x)*2+1];
 }
 DCFlushRange(tiled,WIDTH*ph*2);
}
/* Prepare the shared scratch texture for video-only playback. */
static void swizzle(int height){swizzle_to(height,tiled);}
struct jpeg_error {struct jpeg_error_mgr base;jmp_buf jump;};
/* Return a libjpeg fatal error to the active decoder recovery point. */
static void jpeg_fail(j_common_ptr c){struct jpeg_error *e=(struct jpeg_error*)c->err;longjmp(e->jump,1);}
typedef struct {
 struct jpeg_decompress_struct c;
 struct jpeg_error error;
 int created;
} JPEGDecoder;
/* Initialize the reusable JPEG decompressor and its error handler. */
static int decoder_open(JPEGDecoder *d){
 memset(d,0,sizeof(*d));
 d->c.err=jpeg_std_error(&d->error.base);
 d->error.base.error_exit=jpeg_fail;
 if(setjmp(d->error.jump)){
  if(d->created)jpeg_destroy_decompress(&d->c);
  d->created=0;
  return 0;
 }
 jpeg_create_decompress(&d->c);
 d->created=1;
 return 1;
}
/* Destroy an initialized JPEG decompressor and clear its ownership flag. */
static void decoder_close(JPEGDecoder *d){
 if(d->created){jpeg_destroy_decompress(&d->c);d->created=0;}
}
/* Decode one MJPEG image from jpegdata into big-endian linear RGB565, validating its dimensions. */
static int decode_jpeg(JPEGDecoder *d,size_t n,int *height){
 struct jpeg_decompress_struct *c=&d->c;
 /* Error state resets per image; the decompressor itself is reused. */
 if(setjmp(d->error.jump)){jpeg_abort_decompress(c);return 0;}
 jpeg_mem_src(c,jpegdata,(unsigned long)n);
 if(jpeg_read_header(c,TRUE)!=JPEG_HEADER_OK){jpeg_abort_decompress(c);return 0;}
 if(c->image_width!=WIDTH||c->image_height<2||c->image_height>MAXHEIGHT){jpeg_abort_decompress(c);return 0;}
 c->out_color_space=JCS_RGB;
 if(!jpeg_start_decompress(c)){jpeg_abort_decompress(c);return 0;}
 if(c->output_components!=3||c->output_width!=WIDTH||c->output_height>MAXHEIGHT){jpeg_abort_decompress(c);return 0;}
 JSAMPARRAY row=(*c->mem->alloc_sarray)((j_common_ptr)c,JPOOL_IMAGE,WIDTH*3,1);
 while(c->output_scanline<c->output_height){
  unsigned y=c->output_scanline;
  if(jpeg_read_scanlines(c,row,1)!=1){jpeg_abort_decompress(c);return 0;}
  for(unsigned x=0;x<WIDTH;x++){
   unsigned char *p=&row[0][x*3];
   uint16_t rgb=(uint16_t)(((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3));
   linear[(y*WIDTH+x)*2]=rgb>>8;linear[(y*WIDTH+x)*2+1]=rgb&255;
  }
 }
 *height=(int)c->output_height;
 if(!jpeg_finish_decompress(c)){jpeg_abort_decompress(c);return 0;}
 return 1;
}
/* Recognize the .avi extension without regard to letter case. */
static int isavi(const char *s){size_t n=strlen(s);if(n<4)return 0;return !strcasecmp(s+n-4,".avi");}
/* Enumerate directories and AVI files within the current browser path and reset selection. */
static void list_files(void){
 count=0;selected=scroll=0;DIR *d=opendir(folder);if(!d){snprintf(status,sizeof(status),"Cannot open %.90s",folder);return;}
 struct dirent *ent;while((ent=readdir(d))&&count<MAXFILES){if(!strcmp(ent->d_name,"."))continue;
  if(!strcmp(ent->d_name,".."))continue;
  char full[PATHCAP];if(strlen(folder)+strlen(ent->d_name)+2>=sizeof(full))continue;
  snprintf(full,sizeof(full),"%s%s",folder,ent->d_name);
  struct stat st;if(stat(full,&st))continue;
  if(!S_ISDIR(st.st_mode)&&!isavi(ent->d_name))continue;
  strncpy(entries[count].name,ent->d_name,sizeof(entries[count].name)-1);entries[count].name[sizeof(entries[count].name)-1]=0;
  entries[count].dir=S_ISDIR(st.st_mode);count++;
 }
 closedir(d);
}
/* Navigate to the parent directory without leaving the mounted volume root. */
static void go_up(void){
 size_t root_len=strlen(mount_root);
 size_t n=strlen(folder);
 /* B at the mounted root must not change the selected volume. */
 if(n==root_len && strcmp(folder,mount_root)==0)return;
 /* Recover to the selected volume if the path is outside its root. */
 if(n<=root_len || strncmp(folder,mount_root,root_len)!=0){
  strcpy(folder,mount_root);list_files();return;
 }
 if(folder[n-1]=='/')folder[--n]='\0';
 char *p=strrchr(folder,'/');
 if(p && (size_t)(p+1-folder)>=root_len)*(p+1)='\0';
 else strcpy(folder,mount_root);
 list_files();
}
/* Embedded wallpaper is copied only when the file browser redraws.
   It is never touched during MJPEG playback. XFB is an uncached K1 pointer. */
extern const uint8_t beanz_wallpaper_yuyv[640u * 480u * 2u];
/* Copy the embedded wallpaper to the idle framebuffer, scaling rows to the active video height. */
static void menu_wallpaper_draw(void){
 const unsigned dest_width=vmode->fbWidth;
 const unsigned dest_height=vmode->xfbHeight;
 const unsigned copy_width=dest_width<640u?dest_width:640u;
 uint8_t *dest=(uint8_t *)xfb[fb];
 if(!dest || !dest_height)return;
 for(unsigned y=0;y<dest_height;y++){
  unsigned sy=(unsigned)(((uint64_t)y*480u)/dest_height);
  uint8_t *row=dest+(size_t)y*dest_width*VI_DISPLAY_PIX_SZ;
  memcpy(row,beanz_wallpaper_yuyv+(size_t)sy*640u*2u,(size_t)copy_width*2u);
 }
}
/* Draw the file list, controls, status and diagnostics to the idle framebuffer, then present it. */
static void browser(void){
 /* Draw into the back XFB. Do not modify the XFB VI is scanning out. */
 console_init(xfb[fb],20,20,vmode->fbWidth-20,vmode->xfbHeight-20,
              vmode->fbWidth*VI_DISPLAY_PIX_SZ);
 menu_wallpaper_draw();
 printf("\x1b[5;1HBEANZ AVI Player v1.3\n");
 printf("Please check GitHub for conversion information\n");
 printf("github.com/nxs8739/beanz-avi-player\n");
 printf("Directory: %.55s\n",folder);
 printf("UP/DOWN select | A open | B parent | START exit\n");
 int visible=debug_status[0]?8:15;if(selected<scroll)scroll=selected;if(selected>=scroll+visible)scroll=selected-visible+1;
 for(int i=scroll;i<count&&i<scroll+visible;i++)printf("%c %s %.53s\n",i==selected?'>':' ',entries[i].dir?"[DIR]":"     ",entries[i].name);
 printf("\n%s\n",status);
 if(debug_status[0])printf("%s",debug_status);
 /* Present only after the full menu has been rendered. */
 VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();
 fb^=1; /* Next browser draw (or video frame) uses the idle XFB. */
}
/* Match an AVI video chunk against the selected numbered video stream. */
static int video_chunk(uint32_t id,uint32_t stream){unsigned char a=id&255,b=(id>>8)&255,c=(id>>16)&255,d=(id>>24)&255;
 return a>='0'&&a<='9'&&b>='0'&&b<='9'&&(uint32_t)((a-'0')*10+(b-'0'))==stream&&c=='d'&&(d=='c'||d=='b');}
/* PCM chunk IDs use the same numbered AVI stream ID as the header parser.
   Avi PCM is little-endian; pcm_push_le16 converts to big-endian stereo. */
/* Match an AVI PCM chunk against the selected numbered audio stream. */
static int audio_chunk(uint32_t id,uint32_t stream){
 unsigned a=id&255,b=(id>>8)&255,c=(id>>16)&255,d=(id>>24)&255;
 return a>='0'&&a<='9'&&b>='0'&&b<='9'&&
        (uint32_t)((a-'0')*10+(b-'0'))==stream&&c=='w'&&d=='b';
}
/* Read just a bounded piece of an AVI PCM chunk. Never read SD in the DMA ISR. */
/* Read bounded, sample-aligned PCM pieces and convert them into the output FIFO. */
static int collect_pcm(AVI *a,PCMAudio *pcm,const AVIChunk *c,
                       size_t offset,size_t bytes){
 const size_t align=(size_t)a->audio_channels*2u;
 if(!align || offset>c->size || bytes>(size_t)c->size-offset ||
    offset%align || bytes%align)return 0;
 while(bytes){
  size_t n=bytes;
  if(n>PCM_RAW_SCRATCH)n=PCM_RAW_SCRATCH;
  n-=n%align;
  size_t output=n/align*4u;
  if(output>pcm_ring_room(pcm))pcm_prepare(pcm);
  if(!n || output>pcm_ring_room(pcm) ||
     !avi_read_part(a,c,offset,pcm_scratch,n) ||
     !pcm_push_le16(pcm,pcm_scratch,n,a->audio_channels))return 0;
  bytes-=n;offset+=n;
 }
 return 1;
}
/* Independent read-only audio cursor: keep feeding PCM even if video is
   waiting for the media clock. Same AVI parser and bounded PCM FIFO.
   Only the preparation thread touches either file handle after startup. */
#define PCM_FEED_LOW (2u * PCM_DMA_BYTES)
#define PCM_FEED_BUDGET PCM_RAW_SCRATCH
typedef struct {
 AVI avi;
 AVIChunk chunk;
 size_t offset, raw_read;
 int pending, eof;
} PCMFeed;

/* Fill up to target output bytes, with a bounded amount of new PCM per
   runtime call. Retain partially consumed AVI chunks across calls. */
/* Advance the independent audio cursor until the requested FIFO level or input budget is reached. */
static int pcm_feed_fill(PCMFeed *feed,PCMAudio *pcm,size_t target,size_t budget){
 AVI *a=&feed->avi;
 const size_t align=(size_t)a->audio_channels*2u;
 if(!align)return 0;
 while(!feed->eof && pcm_ring_used(pcm)<target && budget>=4u){
  if(!feed->pending){
   if(!avi_next(a,&feed->chunk)){if(a->read_error)return 0;feed->eof=1;break;}
   if(!audio_chunk(feed->chunk.id,a->audio_id))continue;
   if(feed->chunk.size%align)return 0;
   if(!feed->chunk.size)continue;
   feed->offset=0;feed->pending=1;
  }
  size_t want=target-pcm_ring_used(pcm);
  if(want>budget)want=budget;
  size_t n=want/4u*align;
  size_t remaining=(size_t)feed->chunk.size-feed->offset;
  if(n>remaining)n=remaining;
  if(!n)break;
  if(!collect_pcm(a,pcm,&feed->chunk,feed->offset,n))return 0;
  feed->offset+=n;feed->raw_read+=n;
  budget-=n/align*4u;
  if(feed->offset==feed->chunk.size)feed->pending=0;
 }
 return 1;
}
/* Prepare available DMA blocks and refill PCM when low, including final input staging at EOF. */
static int pcm_feed_service(PCMFeed *feed,PCMAudio *pcm){
 pcm_prepare(pcm);
 if(pcm_ring_used(pcm)<PCM_FEED_LOW){
  if(!pcm_feed_fill(feed,pcm,PCM_FEED_LOW,PCM_FEED_BUDGET))return 0;
  if(feed->eof)pcm_finish_input(pcm);
  else pcm_prepare(pcm);
 }
 return 1;
}
/* Totals stay in RAM; no per-frame console output or storage writes. */
typedef struct {
 uint64_t io_sum,decode_sum,swizzle_sum;
 uint64_t io_max,decode_max,swizzle_max;
 unsigned late;
} PlaybackStats;
/* Accumulate elapsed timer ticks and update the maximum observed interval. */
static void collect_time(uint64_t start,uint64_t stop,uint64_t *total,uint64_t *maximum){
 uint64_t elapsed=diff_ticks(start,stop);
 *total+=elapsed;
 if(elapsed>*maximum)*maximum=elapsed;
}
/* One storage/decoder owner and one GX owner. Mutex protects only metadata;
   never hold it over I/O, JPEG, PCM conversion, GPU or retrace waits. */
typedef struct {
 AVI *a; JPEGDecoder *decoder; PCMAudio *pcm; PCMFeed *feed;
 VideoQueue q; uint8_t *textures; mutex_t lock;
 int stop,started,video_eof,audio_eof,failed;
 uint64_t audio_total; char error[96];
 uint32_t prepared, producer_dropped; PlaybackStats stats;
} VideoPrep;
/* Publish a worker failure and error text under the queue metadata lock. */
static void prep_error(VideoPrep *p,const char *msg){
 LWP_MutexLock(p->lock);p->failed=1;snprintf(p->error,sizeof(p->error),"%s",msg);LWP_MutexUnlock(p->lock);
}
/* Own AVI reads, JPEG decoding and PCM production; refill available video slots and publish prepared textures. */
static void *prepare_video(void *arg){
 VideoPrep *p=arg;uint64_t index=0;
 for(;;){
  LWP_MutexLock(p->lock);
  int stop=p->stop,started=p->started,veof=p->video_eof;
  int slot=(!stop && !veof && (started || p->q.ready<VIDEO_QUEUE_CAP))?vq_reserve(&p->q):-1;
  LWP_MutexUnlock(p->lock);
  if(stop)break;
  /* Startup audio was prefilled by main; ownership transfers after pcm_start. */
  if(started){
   if(!pcm_feed_service(p->feed,p->pcm)){prep_error(p,"PCM lookahead read failed");break;}
   if(p->feed->eof){
    LWP_MutexLock(p->lock);p->audio_eof=1;p->audio_total=p->pcm->diag_input_frames;LWP_MutexUnlock(p->lock);
   }
  }
  if(slot<0){usleep(1000);continue;}
  uint64_t t0=gettime();AVIChunk c;int got=0;
  while(avi_next(p->a,&c)){if(video_chunk(c.id,p->a->video_id)){got=1;break;}}
  if(!got){
   LWP_MutexLock(p->lock);vq_cancel(&p->q,slot);p->video_eof=1;LWP_MutexUnlock(p->lock);
   if(p->a->read_error){prep_error(p,"AVI video read failed");break;}
   continue;
  }
  if(started && vq_index_late(index,pcm_sample_frames_now(p->pcm),PCM_RATE/20u)){
   LWP_MutexLock(p->lock);vq_cancel(&p->q,slot);LWP_MutexUnlock(p->lock);
   index++;p->producer_dropped++;continue;
  }
  if(c.size>MAXJPEG || !avi_read(p->a,&c,jpegdata,MAXJPEG)){prep_error(p,"JPEG chunk too large / read failed");break;}
  uint64_t t1=gettime();int height=0;
  if(!decode_jpeg(p->decoder,c.size,&height)){prep_error(p,"JPEG decoder rejected frame");break;}
  uint64_t t2=gettime();swizzle_to(height,p->textures+(size_t)slot*VIDEO_FRAME_BYTES);uint64_t t3=gettime();
  collect_time(t0,t1,&p->stats.io_sum,&p->stats.io_max);
  collect_time(t1,t2,&p->stats.decode_sum,&p->stats.decode_max);
  collect_time(t2,t3,&p->stats.swizzle_sum,&p->stats.swizzle_max);
  if(diff_ticks(t0,t3)>millisecs_to_ticks(50))p->stats.late++;
  p->prepared++;
  LWP_MutexLock(p->lock);vq_publish(&p->q,slot,index++,height);LWP_MutexUnlock(p->lock);
 }
 return NULL;
}
/* Format playback totals and timing statistics, retaining the counters alongside any error message. */
static void format_stats(const PlaybackStats *s,unsigned prepared,unsigned dropped,unsigned gaps,const AVI *a,const char *error){
 unsigned long d=prepared?prepared:1;
 snprintf(status,sizeof(status),"%s%s%lu fr | avg SD/JPEG/SW %lu/%lu/%lu | max %lu/%lu/%lu ms | slow %u | fill %lu max %lu | pf %lu | audio gap %u skip %u",
 error,error[0]?"\n":"",(unsigned long)frames,
 (unsigned long)ticks_to_millisecs(s->io_sum/d),(unsigned long)ticks_to_millisecs(s->decode_sum/d),(unsigned long)ticks_to_millisecs(s->swizzle_sum/d),
 (unsigned long)ticks_to_millisecs(s->io_max),(unsigned long)ticks_to_millisecs(s->decode_max),(unsigned long)ticks_to_millisecs(s->swizzle_max),s->late,
 (unsigned long)a->refill_calls,(unsigned long)ticks_to_millisecs(a->refill_ticks_max),(unsigned long)a->prefetch_calls,gaps,dropped);
}
/* Play video-only AVI files using the existing single-threaded wall-clock pacing path. */
static void play_silent(AVI *a,JPEGDecoder *decoder){
 PlaybackStats stats={0};uint64_t period=millisecs_to_ticks(50),next_due=gettime();char error[96]="";
 while(playing){
  PAD_ScanPads();uint32_t buttons=PAD_ButtonsDown(0);
  if(buttons&PAD_BUTTON_B)break;
  if(buttons&PAD_BUTTON_A){paused=!paused;if(!paused)next_due=gettime();}
  if(paused){VIDEO_WaitVSync();continue;}
  uint64_t t0=gettime();AVIChunk c;int got=0;
  while(avi_next(a,&c)){if(video_chunk(c.id,a->video_id)){got=1;break;}}
  if(!got){if(a->read_error)strcpy(error,"AVI video read failed");break;}
  if(c.size>MAXJPEG||!avi_read(a,&c,jpegdata,MAXJPEG)){strcpy(error,"JPEG chunk too large / read failed");break;}
  uint64_t t1=gettime();int height=0;
  if(!decode_jpeg(decoder,c.size,&height)){strcpy(error,"JPEG decoder rejected frame");break;}
  uint64_t t2=gettime();swizzle(height);uint64_t t3=gettime();
  collect_time(t0,t1,&stats.io_sum,&stats.io_max);collect_time(t1,t2,&stats.decode_sum,&stats.decode_max);collect_time(t2,t3,&stats.swizzle_sum,&stats.swizzle_max);
  if(diff_ticks(t0,t3)>period)stats.late++;
  while(gettime()<next_due){PAD_ScanPads();if(PAD_ButtonsDown(0)&PAD_BUTTON_B){playing=0;break;}uint64_t t=gettime();if(t<next_due)avi_prefetch(a,(uint32_t)ticks_to_millisecs(diff_ticks(t,next_due)));if(gettime()<next_due)VIDEO_WaitVSync();}
  if(!playing)break;
  show_frame(height);frames++;next_due+=period;uint64_t now=gettime();if(now>next_due+period)next_due=now+period;
 }
 format_stats(&stats,frames,0,0,a,error);
}
/* Validate media, prefill PCM and video, coordinate worker and audio-clock presentation, and clean up after playback. */
static void play_video(const char *path){
 debug_status[0]=0;frames=0;paused=0;playing=1;
 AVI a;char err[128];
 if(!avi_open(&a,path,err,sizeof(err))){snprintf(status,sizeof(status),"%.110s",err);playing=0;return;}
 if(a.fps_num!=20*a.fps_den){avi_close(&a);strcpy(status,"AVI must be 20 FPS");playing=0;return;}
 JPEGDecoder decoder;
 if(!decoder_open(&decoder)){avi_close(&a);strcpy(status,"JPEG decoder init failed");playing=0;return;}
 if(!a.has_audio){play_silent(&a,&decoder);goto done;}
 PCMAudio pcm;PCMFeed feed={0};VideoPrep p={0};
 lwp_t worker;int worker_live=0,mutex_live=0,audio_started=0;
 uint32_t dropped=0,starved=0;unsigned min_ready=VIDEO_QUEUE_CAP;
 uint64_t start=0,last_progress=0,last_samples=0,preload=0;
 long first_lag=0,last_lag=0,min_lag=0,max_lag=0;
 char error[96]="";PCMDebug d={0};
 if(!pcm_open(&pcm)){strcpy(status,"PCM buffer allocation failed");goto done;}
 if(!avi_open(&feed.avi,path,err,sizeof(err))){strcpy(error,"PCM lookahead open failed");goto cleanup;}
 if(!pcm_feed_fill(&feed,&pcm,PCM_PREROLL_BYTES,PCM_PREROLL_BYTES)){strcpy(error,"PCM preload failed");goto cleanup;}
 preload=feed.raw_read;
 if(feed.eof)pcm_finish_input(&pcm);else pcm_prepare(&pcm);
 p.a=&a;p.decoder=&decoder;p.pcm=&pcm;p.feed=&feed;
 p.textures=memalign(32,VIDEO_QUEUE_CAP*VIDEO_FRAME_BYTES);
 if(!p.textures){strcpy(error,"Video queue allocation failed");goto cleanup;}
 if(LWP_MutexInit(&p.lock,0)!=0){strcpy(error,"Video queue mutex failed");goto cleanup;}mutex_live=1;
 /* libogc v3.2.1: larger numeric priority = higher priority.
    Main/presentation explicitly 80; worker 32. */
 LWP_SetThreadPriority(LWP_GetSelf(),80);
 if(LWP_CreateThread(&worker,prepare_video,&p,NULL,64*1024,32)!=0){strcpy(error,"Video preparation thread failed");goto cleanup;}worker_live=1;
 for(;;){
  PAD_ScanPads();if(PAD_ButtonsDown(0)&PAD_BUTTON_B)goto cleanup;
  LWP_MutexLock(p.lock);int failed=p.failed,veof=p.video_eof;unsigned n=p.q.ready;LWP_MutexUnlock(p.lock);
  if(failed)goto cleanup;
  if(n==VIDEO_QUEUE_CAP || veof){if(!n){strcpy(error,"No video frames");goto cleanup;}break;}
  VIDEO_WaitVSync();
 }
 /* Start the media clock only after the initial video reserve is ready. */
 if(!pcm_start(&pcm)){strcpy(error,"PCM DMA could not start");goto cleanup;}
 audio_started=1;start=last_progress=gettime();
 LWP_MutexLock(p.lock);p.started=1;LWP_MutexUnlock(p.lock);
 while(playing){
  PAD_ScanPads();uint32_t buttons=PAD_ButtonsDown(0);
  if(buttons&PAD_BUTTON_B)break;
  if(buttons&PAD_BUTTON_A){paused=!paused;pcm_set_paused(&pcm,paused);last_progress=gettime();last_samples=pcm_sample_frames_now(&pcm);}
  if(paused){VIDEO_WaitVSync();continue;}
  uint64_t samples=pcm_sample_frames_now(&pcm);
  if(samples>last_samples){last_samples=samples;last_progress=gettime();}
  LWP_MutexLock(p.lock);
  int failed=p.failed,veof=p.video_eof,aeof=p.audio_eof;uint64_t total=p.audio_total;
  /* Drop overdue frames only to recover timing; retain the newest available frame. */
  while(vq_should_drop(&p.q,samples,PCM_RATE/20u)){int old=vq_take(&p.q);vq_release(&p.q,old);dropped++;}
  int slot=vq_peek(&p.q);unsigned n=p.q.ready;if(n<min_ready)min_ready=n;
  uint64_t index=slot>=0?p.q.slots[slot].index:0;int height=slot>=0?p.q.slots[slot].height:0;
  int due=slot>=0 && samples>=index*(PCM_RATE/20u);
  if(due)vq_take(&p.q);
  LWP_MutexUnlock(p.lock);
  if(failed)break;
  if(due){
   show_frame_from(height,p.textures+(size_t)slot*VIDEO_FRAME_BYTES);
   LWP_MutexLock(p.lock);vq_release(&p.q,slot);LWP_MutexUnlock(p.lock);
   long lag=(long)(((int64_t)pcm_sample_frames_now(&pcm)-(int64_t)(index*(PCM_RATE/20u)))*1000/PCM_RATE);
   if(!frames||lag<min_lag)min_lag=lag;
   if(!frames||lag>max_lag)max_lag=lag;
   if(!frames)first_lag=lag;
   last_lag=lag;frames++;
   continue;
  }
  if(veof && slot<0 && aeof && samples>=total)break;
  if(aeof && samples>=total && slot>=0){strcpy(error,"Video extends beyond PCM EOF");break;}
  if(ticks_to_millisecs(diff_ticks(last_progress,gettime()))>2000u){strcpy(error,"PCM DMA clock stalled");break;}
  if(slot<0 && !veof)starved++;
  VIDEO_WaitVSync();
 }
cleanup:
 /* Stop playback and join the sole producer before releasing its resources. */
 if(audio_started)pcm_set_paused(&pcm,1);
 uint64_t wall=audio_started?diff_ticks(start,gettime()):0;
 if(worker_live){LWP_MutexLock(p.lock);p.stop=1;LWP_MutexUnlock(p.lock);LWP_JoinThread(worker,NULL);if(p.failed && !error[0])snprintf(error,sizeof(error),"%s",p.error);}
 if(audio_started)pcm_debug_snapshot(&pcm,&d);
 pcm_close(&pcm);
 a.refill_calls+=feed.avi.refill_calls;a.prefetch_calls+=feed.avi.prefetch_calls;
 if(feed.avi.refill_ticks_max>a.refill_ticks_max)a.refill_ticks_max=feed.avi.refill_ticks_max;
 if(audio_started){
  long g[3]={-1,-1,-1};for(unsigned i=0;i<3&&i<d.silence_starts;i++)g[i]=(long)(d.gap_at[i]*1000/PCM_RATE);
  uint64_t excluded=d.silence_frames+d.padding_frames;
  snprintf(debug_status,sizeof(debug_status),"TRACE queued/start %lu/%lu silence %lu ms IRQ %lu\nGap starts ms: %ld / %ld / %ld\nRaw/wall/media ms: %lu/%lu/%lu\nVI lag ms first/last/min/max: %ld/%ld/%ld/%ld\nPCM in %lu fr pre %lu EOF %d\nQ prep %u peak %u min %u empty VI %u pad %lu fr\n",
   (unsigned long)d.queued_gaps,(unsigned long)d.silence_starts,(unsigned long)(d.silence_frames*1000/PCM_RATE),(unsigned long)d.irq_count,g[0],g[1],g[2],
   (unsigned long)(d.clock_frames*1000/PCM_RATE),(unsigned long)ticks_to_millisecs(wall),(unsigned long)((d.clock_frames>=excluded?d.clock_frames-excluded:0)*1000/PCM_RATE),
   first_lag,last_lag,min_lag,max_lag,(unsigned long)d.input_frames,(unsigned long)preload,feed.eof,p.prepared,p.q.peak,min_ready,starved,(unsigned long)d.padding_frames);
 }
 format_stats(&p.stats,p.prepared,dropped+p.producer_dropped,d.queued_gaps,&a,error);
 if(mutex_live)LWP_MutexDestroy(p.lock);
 free(p.textures);avi_close(&feed.avi);
done:
 decoder_close(&decoder);avi_close(&a);playing=0;
}

/* Initialize hardware and storage, run the controller-driven browser, and unmount storage on exit. */
int main(void){
 setup_console();PAD_Init();
 fifo=memalign(32,256*1024);linear=memalign(32,WIDTH*MAXHEIGHT*2);tiled=memalign(32,WIDTH*MAXHEIGHT*2);jpegdata=memalign(32,MAXJPEG);
 if(!fifo||!linear||!tiled||!jpegdata){printf("Out of memory\n");while(1)VIDEO_WaitVSync();}
 /* Wait for EXI device detection to stabilize. */
 for (int i = 0; i < 40; ++i) VIDEO_WaitVSync();
 if (mount_sd("SD2SP2", "sd", &__io_gcsd2)) {
  strcpy(folder, "sd:/"); mounted_name = "sd";
 } else if (mount_sd("SD Gecko Slot B", "cardb", &__io_gcsdb)) {
  strcpy(folder, "cardb:/"); mounted_name = "cardb";
 } else if (mount_sd("SD Gecko Slot A", "carda", &__io_gcsda)) {
  strcpy(folder, "carda:/"); mounted_name = "carda";
 } else {
  printf("No FAT device mounted.\nCheck statuses above.\n");
  while (1) VIDEO_WaitVSync();
 }
 strcpy(mount_root,folder);
 list_files();
 while(1){browser();PAD_ScanPads();uint32_t b=PAD_ButtonsDown(0);
  if(b&PAD_BUTTON_START)break;
  if(b&PAD_BUTTON_UP&&selected>0)selected--;
  if(b&PAD_BUTTON_DOWN&&selected<count-1)selected++;
  if(b&PAD_BUTTON_B)go_up();
  if((b&PAD_BUTTON_A)&&count){char p[PATHCAP];snprintf(p,sizeof(p),"%s%s",folder,entries[selected].name);
   if(entries[selected].dir){if(strlen(p)+2<sizeof(folder)){strcat(p,"/");strcpy(folder,p);list_files();}}
   else {init_gx();play_video(p);}
  }
 }
 if (mounted_name) fatUnmount(mounted_name);return 0;
}
