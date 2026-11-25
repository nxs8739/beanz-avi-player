#ifndef MOCK_GCCORE_H
#define MOCK_GCCORE_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
static inline void DCFlushRange(void *p,size_t n){(void)p;(void)n;}
#endif
