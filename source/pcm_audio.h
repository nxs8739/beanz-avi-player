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
#define PCM_NO_QUEUED       (-2)

typedef struct {
 unsigned char *ring;       /* Single-producer-only normalized BE stereo PCM FIFO */
 size_t write_pos,read_pos,used;
 unsigned char *dma[PCM_DMA_SLOTS];
 unsigned char *silence;
 volatile uint32_t state[PCM_DMA_SLOTS]; /* 0 free, 1 ready, 2 DMA-owned */
 uint32_t valid_bytes[PCM_DMA_SLOTS];
 uint32_t write_slot;
 int input_eof;
 volatile uint32_t end_staged;
 volatile uint32_t current_valid;
 volatile uint64_t padding_done_frames;
 volatile uint32_t next_slot;
 volatile int current_slot;   /* DMA actually being consumed, -1 = silence */
 volatile uint32_t queued_terminal;
 volatile int queued_slot;    /* next DMA; -1 silence, -2 not armed */
 volatile uint32_t current_addr,current_bytes;
 volatile uint32_t queued_addr;
 volatile uint32_t start_irq_pending;
 volatile uint64_t done_frames;
 volatile uint32_t underruns;
 /* Consumed fallback silence is excluded from the movie clock. */
 volatile uint64_t silence_done_frames;
 /* Diagnostic only: never consulted by playback control. */
 volatile uint32_t diag_silence_starts,diag_irq_count;
 volatile uint64_t diag_gap_at[3];
 uint64_t diag_input_frames;
 int running, paused;
} PCMAudio;

int pcm_open(PCMAudio *p);
void pcm_close(PCMAudio *p);
size_t pcm_ring_room(const PCMAudio *p);
size_t pcm_ring_used(const PCMAudio *p);
/* Input is little-endian WAV/AVI PCM; output is big-endian stereo. */
int pcm_push_le16(PCMAudio *p,const unsigned char *src,size_t n,unsigned channels);
void pcm_prepare(PCMAudio *p);
void pcm_finish_input(PCMAudio *p);
int pcm_start(PCMAudio *p);
void pcm_set_paused(PCMAudio *p,int paused);
uint64_t pcm_sample_frames_now(PCMAudio *p);
uint32_t pcm_underruns(const PCMAudio *p);
typedef struct {
 uint64_t clock_frames, silence_frames, padding_frames, input_frames;
 uint64_t gap_at[3];
 uint32_t silence_starts, queued_gaps, irq_count;
} PCMDebug;
void pcm_debug_snapshot(PCMAudio *p,PCMDebug *d);
#endif
