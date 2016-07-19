#ifndef _INTERNALS_H
#define _INTERNALS_H

/*
  //local replacement for: 
  #include "libc.h"
  #include "atomic.h"
  #include "pthread_impl.h"

*/


#define PAGE_SIZE 4096

#if defined(__GNUC__) && defined(__PIC__)
#define inline inline __attribute__((always_inline))
#endif

#define a_and_64 a_and_64
static inline void a_and_64(volatile uint64_t *p, uint64_t v)
{
	__asm__ __volatile(
		"lock ; and %1, %0"
		 : "=m"(*p) : "r"(v) : "memory" );
}

#define a_or_64 a_or_64
static inline void a_or_64(volatile uint64_t *p, uint64_t v)
{
	__asm__ __volatile__(
		"lock ; or %1, %0"
		 : "=m"(*p) : "r"(v) : "memory" );
}

#define a_crash a_crash
static inline void a_crash()
{
	__asm__ __volatile__( "hlt" : : : "memory" );
}

#define a_ctz_64 a_ctz_64
static inline int a_ctz_64(uint64_t x)
{
	__asm__( "bsf %1,%0" : "=r"(x) : "r"(x) );
	return x;
}

#endif
