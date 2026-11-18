/* BEANZ AVI - experimental PCM audio DMA bridge for GameCube libogc 3.2.1.
   No disk I/O, allocations, printf or JPEG work occurs in the DMA callback. */
#include "pcm_audio.h"
#include <gccore.h>
#include <ogc/audio.h>
#include <ogc/irq.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

static PCMAudio *volatile active_pcm;

static void dma_callback(void){
 PCMAudio *p=active_pcm;
 if(!p)return;
 /* The previous 128 ms block has finished; offer the next ready block. */
 if(p->current_slot>=0)p->state[p->current_slot]=0;
 p->done_frames+=PCM_DMA_BYTES/4u;
 uint32_t next=p->next_slot;
 if(p->state[next]==1u){
  p->state[next]=2u;
  p->current_slot=(int)next;
  p->next_slot=(next+1u)%PCM_DMA_SLOTS;
  AUDIO_InitDMA((u32)(uintptr_t)p->dma[next],PCM_DMA_BYTES);
 }else{
  p->current_slot=-1;
  p->underruns++;
  AUDIO_InitDMA((u32)(uintptr_t)p->silence,PCM_DMA_BYTES);
 }
}

int pcm_open(PCMAudio *p){
 memset(p,0,sizeof(*p));
 p->current_slot=-1;
 p->ring=memalign(32,PCM_RING_BYTES);
 if(!p->ring)goto fail;
 for(unsigned i=0;i<PCM_DMA_SLOTS;i++){
  p->dma[i]=memalign(32,PCM_DMA_BYTES);
  if(!p->dma[i])goto fail;
 }
 p->silence=memalign(32,PCM_DMA_BYTES);
 if(!p->silence)goto fail;
 memset(p->silence,0,PCM_DMA_BYTES);
 DCFlushRange(p->silence,PCM_DMA_BYTES);
 return 1;
fail:pcm_close(p);return 0;
}

size_t pcm_ring_room(const PCMAudio *p){return PCM_RING_BYTES-p->used;}
size_t pcm_ring_used(const PCMAudio *p){return p->used;}

int pcm_push_le16(PCMAudio *p,const unsigned char *src,size_t n,unsigned channels){
 if(channels!=1 && channels!=2)return 0;
 size_t align=channels*2u;
 if(n%align)return 0;
 size_t output=n/channels*2u; /* every source sample frame -> 4 output bytes */
 if(output>pcm_ring_room(p))return 0;
 for(size_t i=0;i<n;i+=align){
  unsigned char ll=src[i],lh=src[i+1];
  unsigned char rl=(channels==2)?src[i+2]:ll;
  unsigned char rh=(channels==2)?src[i+3]:lh;
  /* GC audio DMA requires 16-bit signed stereo samples in big-endian order. */
  p->ring[p->write_pos]=lh;p->write_pos=(p->write_pos+1u)%PCM_RING_BYTES;
  p->ring[p->write_pos]=ll;p->write_pos=(p->write_pos+1u)%PCM_RING_BYTES;
  p->ring[p->write_pos]=rh;p->write_pos=(p->write_pos+1u)%PCM_RING_BYTES;
  p->ring[p->write_pos]=rl;p->write_pos=(p->write_pos+1u)%PCM_RING_BYTES;
 }
 p->used+=output;
 return 1;
}

void pcm_prepare(PCMAudio *p){
 if(!p->ring)return;
 /* CPU owns the FIFO and all free slots. ISR only switches ready DMA blocks. */
 for(unsigned n=0;n<PCM_DMA_SLOTS;n++){
  uint32_t idx=p->write_slot;
  if(p->state[idx]!=0u || p->used<PCM_DMA_BYTES)break;
  size_t first=PCM_RING_BYTES-p->read_pos;
  if(first>PCM_DMA_BYTES)first=PCM_DMA_BYTES;
  memcpy(p->dma[idx],p->ring+p->read_pos,first);
  if(first<PCM_DMA_BYTES)
   memcpy(p->dma[idx]+first,p->ring,PCM_DMA_BYTES-first);
  p->read_pos=(p->read_pos+PCM_DMA_BYTES)%PCM_RING_BYTES;
  p->used-=PCM_DMA_BYTES;
  DCFlushRange(p->dma[idx],PCM_DMA_BYTES);
  __sync_synchronize();
  p->state[idx]=1u;
  p->write_slot=(idx+1u)%PCM_DMA_SLOTS;
 }
}

int pcm_start(PCMAudio *p){
 if(p->running)return 1;
 pcm_prepare(p);
 if(p->state[0]!=1u || p->state[1]!=1u)return 0;
 AUDIO_Init(NULL);
 AUDIO_StopDMA();
 AUDIO_SetDSPSampleRate(AI_SAMPLERATE_32KHZ);
 p->done_frames=0;p->underruns=0;
 p->next_slot=1u;p->current_slot=0;p->state[0]=2u;
 p->running=1;p->paused=0;
 active_pcm=p;
 AUDIO_RegisterDMACallback(dma_callback);
 AUDIO_InitDMA((u32)(uintptr_t)p->dma[0],PCM_DMA_BYTES);
 AUDIO_StartDMA();
 return 1;
}

void pcm_set_paused(PCMAudio *p,int paused){
 if(!p->running || !!paused==!!p->paused)return;
 if(paused){AUDIO_StopDMA();p->paused=1;}
 else{AUDIO_StartDMA();p->paused=0;}
}

uint64_t pcm_sample_frames_now(PCMAudio *p){
 if(!p->running)return 0;
 uint32_t level,left;
 uint64_t completed;
 level=IRQ_Disable();
 completed=p->done_frames;
 left=AUDIO_GetDMABytesLeft();
 IRQ_Restore(level);
 if(left>PCM_DMA_BYTES)left=PCM_DMA_BYTES;
 return completed+(uint64_t)(PCM_DMA_BYTES-left)/4u;
}

uint32_t pcm_underruns(const PCMAudio *p){return p->underruns;}

void pcm_close(PCMAudio *p){
 if(p->running){
  AUDIO_StopDMA();
  AUDIO_RegisterDMACallback(NULL);
  active_pcm=NULL;
  p->running=0;
 }
 free(p->ring);
 for(unsigned i=0;i<PCM_DMA_SLOTS;i++)free(p->dma[i]);
 free(p->silence);
 memset(p,0,sizeof(*p));
}
