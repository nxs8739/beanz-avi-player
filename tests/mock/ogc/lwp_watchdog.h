#include <stdint.h>
#include <time.h>
static inline uint64_t gettime(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static inline uint64_t diff_ticks(uint64_t a,uint64_t b){return b-a;}
