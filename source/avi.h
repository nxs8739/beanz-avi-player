#ifndef BEANZ_AVI_H
#define BEANZ_AVI_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* Per-open sequential input cache; each cursor has a single owning thread. */
#define AVI_READ_AHEAD (128u * 1024u)
typedef struct {
 FILE *f;
 long movi_start, movi_end, cursor;
 long riff_end, file_end; /* boundaries for sequential OpenDML continuation */
 uint32_t width,height,fps_num,fps_den,audio_rate,audio_channels,audio_bits;
 uint32_t video_id,audio_id;
 int has_video,has_audio;
 int read_error;
 unsigned char *read_ahead;
 long cache_start;
 size_t cache_length;
 size_t cache_consumed; /* bytes before this offset may be discarded */
 uint32_t refill_calls,prefetch_calls;
 uint64_t refill_bytes,refill_ticks_max;
} AVI;
typedef struct {uint32_t id,size;long data_pos;} AVIChunk;
int avi_open(AVI *a,const char *path,char *err,size_t errlen);
int avi_next(AVI *a,AVIChunk *out);
int avi_read(AVI *a,const AVIChunk *c,void *dest,size_t cap);
/* Read a portion of an AVI chunk without a large temporary allocation. */
int avi_read_part(AVI *a,const AVIChunk *c,size_t offset,void *dest,size_t n);
/* Best-effort synchronous prefetch; available_ms is until display deadline. */
int avi_prefetch(AVI *a,uint32_t available_ms);
void avi_close(AVI *a);
#endif
