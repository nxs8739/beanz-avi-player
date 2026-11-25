#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "frame_queue.h"
static unsigned rng=20261124;static unsigned rnd(void){rng=rng*1664525u+1013904223u;return rng;}
static void sim(const char *label,unsigned vi,unsigned n,unsigned mode){VideoQueue q={0};uint64_t produced=0,last=0;unsigned dropped=0,shown=0,empty=0,maxlag=0,remaining=0;int writing=-1,display=-1;for(unsigned i=0;i<20;i++){int j=vq_reserve(&q);assert(j>=0);vq_publish(&q,j,produced++,240);}assert(vq_reserve(&q)<0);for(unsigned t=0;t<n;t++){if(writing>=0&&remaining&&!--remaining){vq_publish(&q,writing,produced++,240);writing=-1;}if(t%vi==0){if(display>=0){vq_release(&q,display);display=-1;}while(vq_should_drop(&q,(uint64_t)t*32u,1600)){int j=vq_take(&q);assert(j>=0);vq_release(&q,j);dropped++;}int j=vq_peek(&q);if(j<0)empty++;else if(q.slots[j].index*50u<=t){display=vq_take(&q);uint64_t k=q.slots[display].index;assert(!shown||k>last);last=k;shown++;unsigned lag=t-k*50u;if(lag>maxlag)maxlag=lag;}}if(writing<0){writing=vq_reserve(&q);if(writing>=0 && vq_index_late(produced,(uint64_t)t*32u,1600)){vq_cancel(&q,writing);writing=-1;produced++;dropped++;}
if(writing>=0){unsigned r=rnd();remaining=mode==2?65:mode==1?(r%100<4?152:18+r%20):24+r%12;}}assert(q.ready<=20);if(display>=0)assert(q.slots[display].state==VQ_DISPLAYING);}printf("%s: presented=%u dropped=%u empty_retraces=%u max_lag_ms=%u peak=%u\n",label,shown,dropped,empty,maxlag,q.peak);if(mode!=2){assert(dropped==0);assert(empty==0);}else {assert(empty>0);assert(dropped>0);assert(maxlag<250); }}
static void baseline_comparison(void){
 unsigned base=0,buffered=0;unsigned stalls=0;VideoQueue q={0};uint64_t k=0;unsigned remaining=0;int w=-1;
 for(unsigned i=0;i<20;i++){int j=vq_reserve(&q);vq_publish(&q,j,k++,240);}
 for(unsigned t=0;t<60000;t++){
  if(w>=0 && !--remaining){vq_publish(&q,w,k++,240);w=-1;}
  if(t%50==0){int j=vq_peek(&q);if(j>=0&&q.slots[j].index*50u<=t){j=vq_take(&q);vq_release(&q,j);buffered++;}if(t%1000>=150)base++;else stalls++;}
  if(w<0){w=vq_reserve(&q);if(w>=0)remaining=(k%20==0)?152:36;}
 }
 printf("Illustrative blocked presentation vs ready-frame consumption, 60s: blocked=%u queue=%u blocked deadlines=%u (synthetic; not Trace 02 engine)\n",base,buffered,stalls);assert(buffered==1200&&base<buffered);
}
int main(void){baseline_comparison();VideoQueue q={0};for(unsigned r=0;r<10000;r++){int i=vq_reserve(&q);assert(i>=0);vq_cancel(&q,i);i=vq_reserve(&q);vq_publish(&q,i,r,240);assert(vq_take(&q)==i);assert(q.slots[i].index==r);vq_release(&q,i);}puts("PASS queue ownership, cancel, wraparound (10000 iterations)");sim("NTSC approximated 17ms, 73min",17,4380000,0);sim("PAL 20ms, 73min variable stalls",20,4380000,1);sim("Sustained overload, 5min",17,300000,2);}
