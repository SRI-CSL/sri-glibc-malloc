#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdint.h>

#define LOCK_PREFIX	"lock ; "

static inline uint64_t read_64(volatile uint64_t *address){
  return __atomic_load_n(address, __ATOMIC_SEQ_CST);
}

static inline unsigned int cas_64(volatile uint64_t *address, uint64_t old_value, uint64_t new_value)
{
  uint64_t prev = 0;
  
  asm volatile(LOCK_PREFIX "cmpxchgq %1,%2"
	       : "=a"(prev)
	       : "r"(new_value), "m"(*address), "0"(old_value)
	       : "memory");
  
  return prev == old_value;
}

#endif

