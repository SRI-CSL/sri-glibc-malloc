#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdint.h>

typedef struct u128_s {
  uint64_t uno;
  uint64_t dos;
} u128_t;


#define LOCK_PREFIX	"lock ; "

static inline unsigned int cas_64(volatile unsigned long *address, unsigned long old_value, unsigned long new_value)
{
	unsigned long prev = 0;

	asm volatile(LOCK_PREFIX "cmpxchgq %1,%2"
		: "=a"(prev)
		: "r"(new_value), "m"(*address), "0"(old_value)
		: "memory");

	return prev == old_value;
}

static inline unsigned int cas_128(volatile u128_t *address, u128_t old_value, u128_t new_value)
{

  char result;
  asm volatile
    (LOCK_PREFIX "cmpxchg16b %1\n\t"
     "setz %0\n"
     : "=q" ( result )
       , "+m" ( *address )
     : "a" ( old_value.uno ), "d" ( old_value.dos )
       ,"b" ( new_value.uno ), "c" ( new_value.dos )
     : "cc"
     );
  return result;
}

#endif

