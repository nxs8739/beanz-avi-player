#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pcm_audio.h"
#include <ogc/audio.h>
static void (*callback)(void);static uint32_t left,armed;static int active;
void AUDIO_Init(void *x){(void)x;}void AUDIO_StopDMA(void){active=0;}void AUDIO_SetDSPSampleRate(int x){(void)x;}void AUDIO_RegisterDMACallback(void (*cb)(void)){callback=cb;}void AUDIO_InitDMA(uint32_t a,uint32_t n){(void)a;armed=n;}
void AUDIO_StartDMA(void){active=1;left=armed;callback();}uint32_t AUDIO_GetDMABytesLeft(void){return left;}
static void advance(uint32_t n){assert(active && n<=left);left-=n;if(!left){left=armed;callback();}}
static unsigned char data[PCM_RING_BYTES];
static void run(unsigned tail,int pause){PCMAudio p;assert(pcm_open(&p));unsigned bytes=2*PCM_DMA_BYTES+tail;assert(pcm_push_le16(&p,data,bytes,2));pcm_finish_input(&p);assert(pcm_start(&p));advance(PCM_DMA_BYTES);advance(PCM_DMA_BYTES);if(tail){unsigned valid=tail & ~31u;if(valid){advance(valid);assert(pcm_sample_frames_now(&p)==(2*PCM_DMA_BYTES+valid)/4u);}if(pause){uint64_t before=pcm_sample_frames_now(&p);pcm_set_paused(&p,1);assert(pcm_sample_frames_now(&p)==before);pcm_set_paused(&p,0);assert(pcm_sample_frames_now(&p)==before);}advance(left);}assert(pcm_sample_frames_now(&p)==bytes/4u);advance(left);assert(pcm_sample_frames_now(&p)==bytes/4u);assert(!pcm_underruns(&p));PCMDebug d;pcm_debug_snapshot(&p,&d);assert(d.clock_frames-d.silence_frames-d.padding_frames==bytes/4u);pcm_close(&p);}
int main(void){for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)i;for(unsigned tail=0;tail<PCM_DMA_BYTES;tail+=4)run(tail,tail%128==32);PCMAudio p;assert(pcm_open(&p));assert(pcm_push_le16(&p,data,2*PCM_DMA_BYTES,2));assert(pcm_start(&p));advance(left);advance(left);uint64_t before=pcm_sample_frames_now(&p);advance(left);assert(pcm_sample_frames_now(&p)==before);assert(pcm_underruns(&p)>0);assert(pcm_push_le16(&p,data,PCM_DMA_BYTES,2));pcm_prepare(&p);advance(left);advance(left);advance(left);assert(pcm_sample_frames_now(&p)>before);pcm_close(&p);puts("PASS PCM: 4096 final-tail sizes (0..16380, 4-byte increments), pause subset, terminal padding and real gap recovery");}
