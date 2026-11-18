#ifndef BEANZ_PCM_AUDIO_H
#define BEANZ_PCM_AUDIO_H
#include <stddef.h>
#include <stdint.h>

/* All sizes are multiples of 32 for GameCube DMA. 32 kHz x stereo x 16 bits. */
#define PCM_RATE             32000u
#define PCM_RING_BYTES       (256u * 1024u)   /* 2.048 seconds */
#define PCM_PREROLL_BYTES    (128u * 1024u)   /* 1.024 seconds */
#define PCM_DMA_BYTES        (16u * 1024u)    /* 128 ms per DMA block */
#define PCM_DMA_SLOTS        4u
#define PCM_RAW_SCRATCH      8192u

typedef struct {
 unsigned char *ring;       /* Main-thread-only normalized BE stereo PCM FIFO */
 size_t write_pos,read_pos,used;
 unsigned char *dma[PCM_DMA_SLOTS];
 unsigned char *silence;
 volatile uint32_t state[PCM_DMA_SLOTS]; /* 0 free, 1 ready, 2 playing */
 uint32_t write_slot;
 volatile uint32_t next_slot;
 volatile int current_slot;
 volatile uint64_t done_frames;
 volatile uint32_t underruns;
 int running, paused;
} PCMAudio;

int pcm_open(PCMAudio *p);
void pcm_close(PCMAudio *p);
size_t pcm_ring_room(const PCMAudio *p);
size_t pcm_ring_used(const PCMAudio *p);
/* Input is little-endian WAV/AVI PCM; output is big-endian stereo. */
int pcm_push_le16(PCMAudio *p,const unsigned char *src,size_t n,unsigned channels);
void pcm_prepare(PCMAudio *p);
int pcm_start(PCMAudio *p);
void pcm_set_paused(PCMAudio *p,int paused);
uint64_t pcm_sample_frames_now(PCMAudio *p);
uint32_t pcm_underruns(const PCMAudio *p);
#endif
