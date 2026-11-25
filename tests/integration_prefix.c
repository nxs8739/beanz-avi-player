#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <jpeglib.h>
#include "avi.h"
#include "pcm_audio.h"
#include "frame_queue.h"
#include <ogc/lwp_watchdog.h>
#include <gccore.h>
#define WIDTH 320
#define MAXHEIGHT 240
#define MAXJPEG (1024*1024)
#define PAD_BUTTON_A 1u
#define PAD_BUTTON_B 2u
static uint8_t *linear,*tiled,*jpegdata;
static uint8_t pcm_scratch[PCM_RAW_SCRATCH];
static int playing,paused;static uint32_t frames;
static char status[256],debug_status[512];
static uint64_t millisecs_to_ticks(uint64_t x){return x*1000000u;}
static uint64_t ticks_to_millisecs(uint64_t x){return x/1000000u;}
static unsigned stop_after_ms;static uint64_t test_start;
static void PAD_ScanPads(void){}static uint32_t PAD_ButtonsDown(int i){(void)i;return stop_after_ms && ticks_to_millisecs(gettime()-test_start)>=stop_after_ms?PAD_BUTTON_B:0;}
static void VIDEO_WaitVSync(void){usleep(16667);}
static void show_frame_from(int h,const uint8_t *p){assert(h>0&&h<=240&&p);VIDEO_WaitVSync();}
static void show_frame(int h){show_frame_from(h,tiled);}
typedef pthread_mutex_t *mutex_t;typedef pthread_t lwp_t;
static int LWP_MutexInit(mutex_t *p,int n){(void)n;*p=malloc(sizeof(**p));return pthread_mutex_init(*p,NULL);}
static void LWP_MutexLock(mutex_t m){pthread_mutex_lock(m);}static void LWP_MutexUnlock(mutex_t m){pthread_mutex_unlock(m);}static void LWP_MutexDestroy(mutex_t m){pthread_mutex_destroy(m);free(m);}
static lwp_t LWP_GetSelf(void){return pthread_self();}static void LWP_SetThreadPriority(lwp_t t,unsigned p){(void)t;(void)p;}
static int LWP_CreateThread(lwp_t *t,void *(*f)(void *),void *p,void *s,unsigned n,unsigned pr){(void)s;(void)n;(void)pr;return pthread_create(t,NULL,f,p);}static void LWP_JoinThread(lwp_t t,void **p){pthread_join(t,p);}
