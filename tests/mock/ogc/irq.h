#include <stdint.h>
#ifdef THREADED_MOCK
uint32_t IRQ_Disable(void);void IRQ_Restore(uint32_t n);
#else
static inline uint32_t IRQ_Disable(void){return 0;}
static inline void IRQ_Restore(uint32_t n){(void)n;}
#endif
