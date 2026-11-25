#ifndef BEANZ_FRAME_QUEUE_H
#define BEANZ_FRAME_QUEUE_H
#include <stdint.h>
#define VIDEO_QUEUE_CAP 20u
#define VIDEO_FRAME_BYTES (320u*240u*2u)
typedef enum { VQ_FREE, VQ_WRITING, VQ_READY, VQ_DISPLAYING } VQState;
typedef struct { VQState state; uint64_t index; int height; } VQSlot;
typedef struct { VQSlot slots[VIDEO_QUEUE_CAP]; unsigned read,write,ready,peak; } VideoQueue;
/* All calls are serialized by the caller. Payload is written/read outside lock. */
/* Reserve the next free write slot; return -1 if it is still owned. */
static inline int vq_reserve(VideoQueue *q){unsigned i=q->write;if(q->slots[i].state!=VQ_FREE)return -1;q->slots[i].state=VQ_WRITING;return (int)i;}
/* Return an unpublished write slot to the free state. */
static inline void vq_cancel(VideoQueue *q,unsigned i){q->slots[i].state=VQ_FREE;}
/* Publish completed texture metadata, advance the write cursor and update occupancy. */
static inline void vq_publish(VideoQueue *q,unsigned i,uint64_t index,int h){q->slots[i].index=index;q->slots[i].height=h;q->slots[i].state=VQ_READY;q->write=(i+1)%VIDEO_QUEUE_CAP;if(++q->ready>q->peak)q->peak=q->ready;}
/* Return the oldest ready slot without transferring ownership. */
static inline int vq_peek(const VideoQueue *q){return q->slots[q->read].state==VQ_READY?(int)q->read:-1;}
/* Transfer the oldest ready slot to the presenter and advance the read cursor. */
static inline int vq_take(VideoQueue *q){int i=vq_peek(q);if(i>=0){q->slots[i].state=VQ_DISPLAYING;q->read=(q->read+1)%VIDEO_QUEUE_CAP;q->ready--;}return i;}
/* Release a displayed or deliberately discarded slot for reuse. */
static inline void vq_release(VideoQueue *q,unsigned i){q->slots[i].state=VQ_FREE;}
/* Test whether the movie clock is beyond the existing two-frame lateness tolerance. */
static inline int vq_index_late(uint64_t index,uint64_t samples,unsigned period){return samples>(index+2u)*period;}
/* Existing two-frame lateness tolerance. Drop only if a newer frame is due. */
/* Allow dropping an overdue ready frame only when another ready frame remains. */
static inline int vq_should_drop(const VideoQueue *q,uint64_t samples,unsigned period){int i=vq_peek(q);return i>=0 && q->ready>1 && vq_index_late(q->slots[i].index,samples,period);}
#endif
