#include <ogc/audio.h>
static pthread_mutex_t irq=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static void update_dma(void);
uint32_t IRQ_Disable(void){pthread_mutex_lock(&irq);update_dma();return 0;}void IRQ_Restore(uint32_t n){(void)n;pthread_mutex_unlock(&irq);}
static void (*cb)(void);static uint32_t left,armed;static int active;static uint64_t at;
void AUDIO_Init(void *p){(void)p;}void AUDIO_SetDSPSampleRate(int i){(void)i;}void AUDIO_RegisterDMACallback(void(*f)(void)){cb=f;}
void AUDIO_InitDMA(uint32_t a,uint32_t n){(void)a;armed=n;}void AUDIO_StartDMA(void){active=1;left=armed;at=gettime();cb();}void AUDIO_StopDMA(void){active=0;}
static void update_dma(void){if(active){uint64_t now=gettime();uint64_t blocks=(now-at)/250000u;at+=blocks*250000u;uint64_t bytes=blocks*32u;while(bytes>=left&&left){bytes-=left;left=armed;cb();}left-=(uint32_t)bytes;}}
uint32_t AUDIO_GetDMABytesLeft(void){return left;}
/* Wrap actual file reads, injecting bounded latency only into large reads.
   Host sleep models a preemptible storage wait, not an EXI critical section. */
static unsigned stall_enabled,read_number,stall_count;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t size,size_t n,FILE *f){
 if(stall_enabled && size*n>=8192 && ++read_number%17==0){stall_count++;usleep(stall_count%2?75000:152000);}
 return __real_fread(p,size,n,f);
}
int main(int argc,char **argv){linear=malloc(VIDEO_FRAME_BYTES);tiled=malloc(VIDEO_FRAME_BYTES);jpegdata=malloc(MAXJPEG);assert(linear&&tiled&&jpegdata);for(int i=1;i<argc;i++){test_start=gettime();stop_after_ms=0;stall_enabled=(i==3);play_video(argv[i]);stall_enabled=0;printf("%s\n%s\n%s",argv[i],status,debug_status);assert(frames>0);assert(!strstr(status,"failed")&&!strstr(status,"stalled")&&!strstr(status,"extends")&&!strstr(status,"rejected"));assert(strstr(status,"audio gap 0"));assert(strstr(debug_status,"EOF 1"));}printf("Injected large-read stalls (75/152 ms): %u\n",stall_count);assert(stall_count>0);test_start=gettime();stop_after_ms=700;play_video(argv[1]);assert(!strstr(status,"failed"));printf("Manual stop: %s\n",status);free(linear);free(tiled);free(jpegdata);puts("PASS actual playback worker, decoder, AVI and PCM with POSIX/mock hardware; natural EOF, repeated open, B stop");return 0;}
