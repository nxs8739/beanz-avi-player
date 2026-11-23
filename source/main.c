/* BEANZ AVI Test - experimental libogc / libjpeg build. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
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
static char status[160]="Ready";
static char folder[PATHCAP]="sd:/";
/* Keep browser navigation within the volume selected at startup. */
static char mount_root[16]="sd:/";
static struct {char name[256];int dir;} entries[MAXFILES];
static int count=0,selected=0,scroll=0;
static const char *mounted_name = NULL;
static uint32_t read32(const char *s){return FCC(s[0],s[1],s[2],s[3]);}
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
static void setup_console(void){
 VIDEO_Init();vmode=VIDEO_GetPreferredMode(NULL);
 xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
 VIDEO_Configure(vmode);VIDEO_SetNextFramebuffer(xfb[0]);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 console_init(xfb[0],20,20,vmode->fbWidth-20,vmode->xfbHeight-20,vmode->fbWidth*VI_DISPLAY_PIX_SZ);
 fb=1; /* Offscreen XFB for the first browser redraw. */
}
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
static void show_frame(int height){
 int padded=(height+3)&~3;
 GXTexObj tex;GX_InitTexObj(&tex,tiled,WIDTH,padded,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,GX_FALSE);
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
static void swizzle(int height){
 int ph=(height+3)&~3;memset(tiled,0,WIDTH*ph*2);
 for(int y=0;y<height;y++)for(int x=0;x<WIDTH;x++){
  size_t o=((size_t)(y>>2)*(WIDTH>>2)+(x>>2))*32+((y&3)*4+(x&3))*2;
  tiled[o]=linear[(y*WIDTH+x)*2];tiled[o+1]=linear[(y*WIDTH+x)*2+1];
 }
 DCFlushRange(tiled,WIDTH*ph*2);
}
struct jpeg_error {struct jpeg_error_mgr base;jmp_buf jump;};
static void jpeg_fail(j_common_ptr c){struct jpeg_error *e=(struct jpeg_error*)c->err;longjmp(e->jump,1);}
typedef struct {
 struct jpeg_decompress_struct c;
 struct jpeg_error error;
 int created;
} JPEGDecoder;
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
static void decoder_close(JPEGDecoder *d){
 if(d->created){jpeg_destroy_decompress(&d->c);d->created=0;}
}
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
static int isavi(const char *s){size_t n=strlen(s);if(n<4)return 0;return !strcasecmp(s+n-4,".avi");}
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
static void browser(void){
 /* Draw into the back XFB. Do not modify the XFB VI is scanning out. */
 console_init(xfb[fb],20,20,vmode->fbWidth-20,vmode->xfbHeight-20,
              vmode->fbWidth*VI_DISPLAY_PIX_SZ);
 menu_wallpaper_draw();
 printf("\x1b[5;1HBEANZ AVI PLAYER - EXPERIMENTAL VIDEO TEST\n");
 printf("Directory: %.55s\n",folder);
 printf("UP/DOWN select | A open | B parent | START exit\n\n");
 int visible=15;if(selected<scroll)scroll=selected;if(selected>=scroll+visible)scroll=selected-visible+1;
 for(int i=scroll;i<count&&i<scroll+visible;i++)printf("%c %s %.53s\n",i==selected?'>':' ',entries[i].dir?"[DIR]":"     ",entries[i].name);
 printf("\n%s\n",status);
 /* Present only after the full menu has been rendered. */
 VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();
 fb^=1; /* Next browser draw (or video frame) uses the idle XFB. */
}
static int video_chunk(uint32_t id,uint32_t stream){unsigned char a=id&255,b=(id>>8)&255,c=(id>>16)&255,d=(id>>24)&255;
 return a>='0'&&a<='9'&&b>='0'&&b<='9'&&((a-'0')*10+(b-'0'))==stream&&c=='d'&&(d=='c'||d=='b');}
/* PCM chunk IDs use the same numbered AVI stream ID as the header parser.
   Avi PCM is little-endian; pcm_push_le16 converts to big-endian stereo. */
static int audio_chunk(uint32_t id,uint32_t stream){
 unsigned a=id&255,b=(id>>8)&255,c=(id>>16)&255,d=(id>>24)&255;
 return a>='0'&&a<='9'&&b>='0'&&b<='9'&&
        ((a-'0')*10+(b-'0'))==stream&&c=='w'&&d=='b';
}
/* Read just a bounded piece of an AVI PCM chunk. Never read SD in the DMA ISR. */
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
/* Preroll roughly one second of audio without holding JPEGs in RAM.
   Then rewind only the AVI movi logical cursor; the cache manages the seek.
   During playback, already-staged PCM bytes are skipped exactly once. */
static int preload_pcm(AVI *a,PCMAudio *pcm,size_t *already_read){
 *already_read=0;
 AVIChunk c;
 const size_t align=(size_t)a->audio_channels*2u;
 while(pcm_ring_used(pcm)<PCM_PREROLL_BYTES && avi_next(a,&c)){
  if(!audio_chunk(c.id,a->audio_id))continue;
  if(c.size%align)return 0;
  size_t want_out=PCM_PREROLL_BYTES-pcm_ring_used(pcm);
  size_t want_raw=want_out/4u*align;
  size_t take=c.size;
  if(take>want_raw)take=want_raw;
  take-=take%align;
  if(take && !collect_pcm(a,pcm,&c,0,take))return 0;
  *already_read+=take;
 }
 a->cursor=a->movi_start;
 return pcm_ring_used(pcm)>=PCM_DMA_BYTES*2u;
}
/* Totals stay in RAM; no per-frame console output or storage writes. */
typedef struct {
 uint64_t io_sum,decode_sum,swizzle_sum;
 uint64_t io_max,decode_max,swizzle_max;
 unsigned late;
} PlaybackStats;
static void collect_time(uint64_t start,uint64_t stop,uint64_t *total,uint64_t *maximum){
 uint64_t elapsed=diff_ticks(start,stop);
 *total+=elapsed;
 if(elapsed>*maximum)*maximum=elapsed;
}
static void play_video(const char *path){
 AVI a;char err[128];
 if(!avi_open(&a,path,err,sizeof(err))){snprintf(status,sizeof(status),"%.110s",err);return;}
 if(a.fps_num!=20*a.fps_den){avi_close(&a);strcpy(status,"AVI must be 20 FPS");return;}
 JPEGDecoder decoder;
 if(!decoder_open(&decoder)){avi_close(&a);strcpy(status,"JPEG decoder init failed");return;}
 PCMAudio pcm;
 int with_audio=a.has_audio;
 size_t preroll_raw=0;
 if(with_audio){
  if(!pcm_open(&pcm)){
   decoder_close(&decoder);avi_close(&a);
   strcpy(status,"PCM buffer allocation failed");return;
  }
  if(!preload_pcm(&a,&pcm,&preroll_raw)){
   pcm_close(&pcm);decoder_close(&decoder);avi_close(&a);
   strcpy(status,"PCM preload failed / too little audio");return;
  }
  pcm_prepare(&pcm);
 }
 playing=1;paused=0;frames=0;
 strcpy(status,"Ready");
 PlaybackStats stats={0};
 const uint64_t frame_period=millisecs_to_ticks(50);
 uint64_t next_due=gettime();
 uint64_t video_index=0;
 uint32_t dropped=0;
 int audio_started=0;
 while(playing){
  PAD_ScanPads();uint32_t buttons=PAD_ButtonsDown(0);
  if(buttons&PAD_BUTTON_B){playing=0;break;}
  if(buttons&PAD_BUTTON_A){
   paused=!paused;
   if(with_audio && audio_started)pcm_set_paused(&pcm,paused);
   if(!paused && !with_audio)next_due=gettime();
  }
  if(paused){VIDEO_WaitVSync();continue;}

  uint64_t t0=gettime();
  AVIChunk c;int got=0;
  while(avi_next(&a,&c)){
   if(with_audio && audio_chunk(c.id,a.audio_id)){
    const size_t align=(size_t)a.audio_channels*2u;
    if(c.size%align){strcpy(status,"Unaligned PCM chunk");playing=0;break;}
    size_t offset=0;
    if(preroll_raw){
     offset=preroll_raw<c.size?preroll_raw:(size_t)c.size;
     preroll_raw-=offset;
    }
    if(offset<c.size && !collect_pcm(&a,&pcm,&c,offset,c.size-offset)){
     strcpy(status,"PCM read failed / audio queue full");playing=0;break;
    }
   }
   if(video_chunk(c.id,a.video_id)){got=1;break;}
  }
  if(!playing || !got)break;
  if(with_audio && audio_started){
   /* When a frame is over one frame late, skip its JPEG instead of
      displaying multiple frames in a catch-up burst. */
   uint64_t samples=pcm_sample_frames_now(&pcm);
   if(samples>(video_index+2u)*(PCM_RATE/20u)){
    video_index++;dropped++;pcm_prepare(&pcm);continue;
   }
  }
  if(c.size>MAXJPEG||!avi_read(&a,&c,jpegdata,MAXJPEG)){
   strcpy(status,"JPEG chunk too large / read failed");break;
  }
  uint64_t t1=gettime();
  int height=0;
  if(!decode_jpeg(&decoder,c.size,&height)){
   strcpy(status,"JPEG decoder rejected frame");break;
  }
  uint64_t t2=gettime();
  swizzle(height);
  uint64_t t3=gettime();
  collect_time(t0,t1,&stats.io_sum,&stats.io_max);
  collect_time(t1,t2,&stats.decode_sum,&stats.decode_max);
  collect_time(t2,t3,&stats.swizzle_sum,&stats.swizzle_max);
  if(diff_ticks(t0,t3)>frame_period)stats.late++;

  if(with_audio){
   pcm_prepare(&pcm);
   if(!audio_started){
    /* Start the sample clock only once the first video frame is ready. */
    if(!pcm_start(&pcm)){
     strcpy(status,"PCM DMA could not start");break;
    }
    audio_started=1;
   }
   uint64_t target_samples=video_index*(PCM_RATE/20u);
   uint64_t last_samples=pcm_sample_frames_now(&pcm);
   uint64_t last_progress=gettime();
   while(playing && pcm_sample_frames_now(&pcm)<target_samples){
    PAD_ScanPads();
    uint32_t wait_buttons=PAD_ButtonsDown(0);
    if(wait_buttons&PAD_BUTTON_B){playing=0;break;}
    if(wait_buttons&PAD_BUTTON_A){
     /* A press during the audio-clock wait must not be consumed/lost.
        Resume the same decoded picture against the same PCM target. */
     paused=1;pcm_set_paused(&pcm,1);
     while(playing && paused){
      VIDEO_WaitVSync();
      PAD_ScanPads();uint32_t pause_buttons=PAD_ButtonsDown(0);
      if(pause_buttons&PAD_BUTTON_B){playing=0;break;}
      if(pause_buttons&PAD_BUTTON_A){
       paused=0;pcm_set_paused(&pcm,0);
       last_samples=pcm_sample_frames_now(&pcm);
       last_progress=gettime(); /* exclude time spent paused from watchdog */
      }
     }
     if(!playing)break;
    }
    pcm_prepare(&pcm);
    uint64_t samples=pcm_sample_frames_now(&pcm);
    if(samples>=target_samples)break;
    if(samples>last_samples){last_samples=samples;last_progress=gettime();}
    /* A stalled DMA must not trap the movie in an endless VSync loop. */
    if(ticks_to_millisecs(diff_ticks(last_progress,gettime()))>2000u){
     strcpy(status,"PCM DMA clock stalled");playing=0;break;
    }
    uint64_t slack_ms=(target_samples-samples)*1000u/PCM_RATE;
    if(slack_ms>0xffffffffULL)slack_ms=0xffffffffULL;
    avi_prefetch(&a,(uint32_t)slack_ms);
    if(pcm_sample_frames_now(&pcm)<target_samples)VIDEO_WaitVSync();
   }
  }else{
   /* The verified video-only path is retained unchanged for silent AVIs. */
   while(gettime()<next_due){
    PAD_ScanPads();
    if(PAD_ButtonsDown(0)&PAD_BUTTON_B){playing=0;break;}
    uint64_t t=gettime();
    if(t<next_due){
     uint64_t slack_ms=ticks_to_millisecs(diff_ticks(t,next_due));
     if(slack_ms>0xffffffffULL)slack_ms=0xffffffffULL;
     avi_prefetch(&a,(uint32_t)slack_ms);
    }
    if(gettime()<next_due)VIDEO_WaitVSync();
   }
  }
  if(!playing)break;
  show_frame(height);
  frames++;video_index++;
  if(!with_audio){
   next_due+=frame_period;
   uint64_t now=gettime();
   if(now>next_due+frame_period)next_due=now+frame_period;
  }
 }
 /* Stop interrupts before freeing PCM RAM or returning to the browser. */
 uint32_t audio_gaps=0;
 if(with_audio){audio_gaps=pcm_underruns(&pcm);pcm_close(&pcm);}
 uint32_t refill_calls=a.refill_calls,prefetch_calls=a.prefetch_calls;
 uint64_t max_refill_ticks=a.refill_ticks_max;
 decoder_close(&decoder);
 avi_close(&a);playing=0;
 /* The next browser() redraw restores the console on its inactive XFB. */
 if(!strcmp(status,"Ready")){
  unsigned long ioavg=frames?(unsigned long)ticks_to_millisecs(stats.io_sum/frames):0;
  unsigned long decavg=frames?(unsigned long)ticks_to_millisecs(stats.decode_sum/frames):0;
  unsigned long swavg=frames?(unsigned long)ticks_to_millisecs(stats.swizzle_sum/frames):0;
  snprintf(status,sizeof(status),
   "%lu fr | avg SD/JPEG/SW %lu/%lu/%lu | max %lu/%lu/%lu ms | slow %u | fill %lu max %lu | pf %lu | audio gap %lu skip %lu",
   (unsigned long)frames,ioavg,decavg,swavg,
   (unsigned long)ticks_to_millisecs(stats.io_max),
   (unsigned long)ticks_to_millisecs(stats.decode_max),
   (unsigned long)ticks_to_millisecs(stats.swizzle_max),stats.late,
   (unsigned long)refill_calls,(unsigned long)ticks_to_millisecs(max_refill_ticks),
   (unsigned long)prefetch_calls,(unsigned long)audio_gaps,(unsigned long)dropped);
 }
}

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
