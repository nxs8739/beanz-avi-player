/* BEANZ AVI - PCM audio DMA bridge for GameCube libogc 3.2.1.
   No disk I/O, allocations, printf or JPEG work occurs in the DMA callback. */
#include "pcm_audio.h"
#include <gccore.h>
#include <ogc/audio.h>
#include <ogc/irq.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

static PCMAudio *volatile active_pcm;

/* AID fires when a DMA transfer starts (and latches the next transfer's
   registers), not when its audio has finished playing.  Keep both the active
   and next transfers owned until the following start interrupt. */
/* Arm the next ready DMA block or substitute silence; distinguish terminal silence from underruns. */
static void dma_queue_next(PCMAudio *p){
 if(p->queued_slot==PCM_NO_QUEUED){
  uint32_t next=p->next_slot;
  if(p->state[next]==1u){
   p->queued_terminal=0;
   p->state[next]=2u;
   p->queued_slot=(int)next;
   p->next_slot=(next+1u)%PCM_DMA_SLOTS;
   p->queued_addr=(u32)(uintptr_t)p->dma[next];
  }else{
   p->queued_terminal=p->end_staged;
   p->queued_slot=-1;  /* silence; don't consume a ring slot */
   p->queued_addr=(u32)(uintptr_t)p->silence;
   if(!p->end_staged)p->underruns++;
  }
 }
 AUDIO_InitDMA(p->queued_addr,PCM_DMA_BYTES);
}

/* Handle DMA start interrupts, retire the previous transfer and track real samples, silence and padding. */
static void dma_callback(void){
 PCMAudio *p=active_pcm;
 if(!p || !p->running || p->paused)return;
 p->diag_irq_count++;
 /* Starting (or restarting) the current transfer is not completion. */
 if(p->start_irq_pending){
  p->start_irq_pending=0;
  dma_queue_next(p);
  return;
 }
 /* The next queued transfer has JUST started.  Only now is the previous
    transfer complete and its slot safe to reuse. */
 if(p->current_slot>=0)p->state[p->current_slot]=0u;
 if(p->current_slot==-1)p->silence_done_frames+=(uint64_t)p->current_bytes/4u;
 else p->padding_done_frames+=(p->current_bytes-p->current_valid)/4u;
 p->done_frames+=(uint64_t)p->current_bytes/4u;
 p->current_slot=p->queued_slot;
 if(p->current_slot==-1 && !p->queued_terminal){
  uint32_t i=p->diag_silence_starts++;
  if(i<3u)p->diag_gap_at[i]=p->done_frames;
 }
 p->current_addr=p->queued_addr;
 p->current_bytes=PCM_DMA_BYTES;
 p->current_valid=p->current_slot>=0?p->valid_bytes[p->current_slot]:0;
 p->queued_slot=PCM_NO_QUEUED;
 dma_queue_next(p);
}

/* Allocate the aligned PCM FIFO, DMA slots and permanent silence buffer. */
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

/* Return the number of free bytes in the PCM producer FIFO. */
size_t pcm_ring_room(const PCMAudio *p){return PCM_RING_BYTES-p->used;}
/* Return the number of PCM bytes currently waiting in the producer FIFO. */
size_t pcm_ring_used(const PCMAudio *p){return p->used;}

/* Convert little-endian mono or stereo samples to big-endian stereo and append them to the FIFO. */
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
 p->diag_input_frames+=(uint64_t)output/4u;
 return 1;
}

/* Stage FIFO audio into free DMA slots; zero-pad the final partial block while tracking its valid bytes. */
void pcm_prepare(PCMAudio *p){
 if(!p->ring)return;
 /* CPU owns the FIFO and all free slots. ISR only switches ready DMA blocks. */
 for(unsigned n=0;n<PCM_DMA_SLOTS;n++){
  uint32_t idx=p->write_slot;
  if(p->state[idx]!=0u || !p->used || (p->used<PCM_DMA_BYTES && !p->input_eof))break;
  size_t valid=p->used<PCM_DMA_BYTES?p->used:PCM_DMA_BYTES;
  size_t first=PCM_RING_BYTES-p->read_pos;
  if(first>valid)first=valid;
  memcpy(p->dma[idx],p->ring+p->read_pos,first);
  if(first<valid)
   memcpy(p->dma[idx]+first,p->ring,valid-first);
  memset(p->dma[idx]+valid,0,PCM_DMA_BYTES-valid);
  p->valid_bytes[idx]=(uint32_t)valid;
  p->read_pos=(p->read_pos+valid)%PCM_RING_BYTES;
  p->used-=valid;
  DCFlushRange(p->dma[idx],PCM_DMA_BYTES);
  __sync_synchronize();
  uint32_t level=IRQ_Disable();
  p->state[idx]=1u;
  if(p->input_eof && !p->used)p->end_staged=1u;
  IRQ_Restore(level);
  p->write_slot=(idx+1u)%PCM_DMA_SLOTS;
 }
 if(p->input_eof && !p->used)p->end_staged=1u;
}
/* Mark the PCM source exhausted and stage any remaining audio for DMA. */
void pcm_finish_input(PCMAudio *p){p->input_eof=1;pcm_prepare(p);}

/* Initialize the 32 kHz audio DMA and start the first staged transfer with its callback registered. */
int pcm_start(PCMAudio *p){
 if(p->running)return 1;
 pcm_prepare(p);
 if(p->state[0]!=1u || (p->state[1]!=1u && !p->end_staged))return 0;
 AUDIO_Init(NULL);
 AUDIO_StopDMA();
 AUDIO_SetDSPSampleRate(AI_SAMPLERATE_32KHZ);
 p->done_frames=0;p->underruns=0;
 p->next_slot=1u;p->current_slot=0;p->state[0]=2u;
 p->current_addr=(u32)(uintptr_t)p->dma[0];
 p->current_bytes=PCM_DMA_BYTES;
 p->current_valid=p->current_slot>=0?p->valid_bytes[p->current_slot]:0;
 p->queued_slot=PCM_NO_QUEUED;
 p->start_irq_pending=1u; /* first IRQ marks transfer start */
 p->running=1;p->paused=0;
 active_pcm=p;
 AUDIO_RegisterDMACallback(dma_callback);
 AUDIO_InitDMA((u32)(uintptr_t)p->dma[0],PCM_DMA_BYTES);
 AUDIO_StartDMA();
 return 1;
}

/* Pause at a DMA block boundary or resume the unread tail without counting it twice. */
void pcm_set_paused(PCMAudio *p,int paused){
 if(!p->running || !!paused==!!p->paused)return;
 uint32_t level=IRQ_Disable();
 if(paused){
  /* Freeze the real transfer first, then capture its unread tail.
     The DSP reports 32-byte DMA blocks; never program a zero-length DMA. */
  uint32_t before_stop=AUDIO_GetDMABytesLeft();
  AUDIO_StopDMA();
  uint32_t left=AUDIO_GetDMABytesLeft();
  /* On a DSP that clears its readable countdown when disabled, use the
     snapshot taken immediately before stopping, with interrupts masked. */
  if(!left)left=before_stop;
  if(left>p->current_bytes)left=p->current_bytes;
  left&=~31u;
  if(!left)left=32u; /* replay at most one 0.25 ms DMA block */
  uint32_t played=p->current_bytes-left;
  if(p->current_slot==-1)p->silence_done_frames+=(uint64_t)played/4u;
  else {uint32_t valid=played<p->current_valid?played:p->current_valid;
   p->padding_done_frames+=(played-valid)/4u;p->current_valid-=valid;}
  p->done_frames+=(uint64_t)played/4u;
  p->current_addr+=played;
  p->current_bytes=left;
  p->paused=1;
 }else{
  /* DMA enable relatches address/length. Resume the unread tail of the
     SAME buffer, then re-arm the previously queued next transfer in its
     start interrupt. No audio slot is released by a restart interrupt. */
  AUDIO_InitDMA(p->current_addr,p->current_bytes);
  p->start_irq_pending=1u;
  p->paused=0;
  AUDIO_StartDMA();
 }
 IRQ_Restore(level);
}

/* Snapshot the movie audio clock, excluding fallback silence and final DMA padding. */
uint64_t pcm_sample_frames_now(PCMAudio *p){
 if(!p->running)return 0;
 uint32_t level,left,bytes,valid;
 uint64_t completed, silence;
 int slot;
 level=IRQ_Disable();
 completed=p->done_frames;
 silence=p->silence_done_frames+p->padding_done_frames;
 slot=p->current_slot;
 bytes=p->current_bytes;valid=p->current_valid;
 left=p->paused?bytes:AUDIO_GetDMABytesLeft();
 IRQ_Restore(level);
 if(left>bytes)left=bytes;
 /* Video follows actual movie PCM, not fallback silence. The raw DMA
    clock is retained separately in pcm_debug_snapshot for comparison. */
 uint64_t media=completed>=silence?completed-silence:0;
 if(slot>=0){uint32_t played=bytes-left;media+=(played<valid?played:valid)/4u;}
 return media;
}

/* Return the count of queued nonterminal silence substitutions. */
uint32_t pcm_underruns(const PCMAudio *p){return p->underruns;}

/* Snapshot the same countdown used by the existing clock. This estimates
   DMA-consumed samples; it cannot measure analog speaker output. */
/* Capture the DMA clock, silence, padding, input and interrupt counters for diagnostics. */
void pcm_debug_snapshot(PCMAudio *p,PCMDebug *d){
 uint32_t level=IRQ_Disable();
 uint32_t bytes=p->current_bytes;
 uint32_t left=p->running?(p->paused?bytes:AUDIO_GetDMABytesLeft()):bytes;
 if(left>bytes)left=bytes;
 uint64_t partial=(uint64_t)(bytes-left)/4u;
 d->clock_frames=p->done_frames+partial;
 d->silence_frames=p->silence_done_frames+(p->current_slot==-1?partial:0u);
 d->padding_frames=p->padding_done_frames;
 if(p->current_slot>=0 && bytes-left>p->current_valid)d->padding_frames+=(bytes-left-p->current_valid)/4u;
 d->input_frames=p->diag_input_frames;
 d->silence_starts=p->diag_silence_starts;
 d->queued_gaps=p->underruns;
 d->irq_count=p->diag_irq_count;
 for(unsigned i=0;i<3;i++)d->gap_at[i]=p->diag_gap_at[i];
 IRQ_Restore(level);
}

/* Stop active DMA, unregister its callback and release PCM allocations. */
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
