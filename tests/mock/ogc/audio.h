#include <stdint.h>
#define AI_SAMPLERATE_32KHZ 0
void AUDIO_Init(void *x);void AUDIO_StopDMA(void);void AUDIO_SetDSPSampleRate(int x);void AUDIO_RegisterDMACallback(void (*cb)(void));void AUDIO_InitDMA(uint32_t addr,uint32_t n);void AUDIO_StartDMA(void);uint32_t AUDIO_GetDMABytesLeft(void);
