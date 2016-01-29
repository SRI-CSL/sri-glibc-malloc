#ifndef __SYNCHRO_ATOMIC_H__
#define __SYNCHRO_ATOMIC_H__

typedef struct {
  uintptr_t 	ptr;
  uint64_t      tag;
} aba_128_t;


//typedef unsigned __int128 uint128_t;


#define mb()		asm volatile ("sync" : : : "memory")
#define LOCK_PREFIX	"lock ; "

static inline unsigned long fetch_and_store(volatile unsigned int *address, unsigned int value)
{
	asm volatile("xchgl %k0,%1"
		: "=r" (value)
		: "m" (*address), "0" (value)
		: "memory");

	return value;
}

static inline int atmc_fetch_and_add(volatile unsigned int *address, int value)
{
	int prev = value;

	asm volatile(
		LOCK_PREFIX "xaddl %0, %1"
		: "+r" (value), "+m" (*address)
		: : "memory");

	return prev + value;
}

static inline void atmc_add32(volatile unsigned int* address, int value)
{
	asm volatile(
		LOCK_PREFIX "addl %1,%0"
		: "=m" (*address)
		: "ir" (value), "m" (*address));
}

static inline void atmc_add64(volatile unsigned long long* address, unsigned long long value)
{
	asm volatile(
		LOCK_PREFIX "addq %1,%0"
		: "=m" (*address)
		: "ir" (value), "m" (*address));
}

static inline unsigned int compare_and_swap32(volatile unsigned int *address, unsigned int old_value, unsigned int new_value)
{
	unsigned long prev = 0;

	asm volatile(LOCK_PREFIX "cmpxchgl %k1,%2"
		: "=a"(prev)
		: "r"(new_value), "m"(*address), "0"(old_value)
		: "memory");

	return prev == old_value;
}

static inline unsigned int compare_and_swap64(volatile unsigned long *address, unsigned long old_value, unsigned long new_value)
{
	unsigned long prev = 0;

	asm volatile(LOCK_PREFIX "cmpxchgq %1,%2"
		: "=a"(prev)
		: "r"(new_value), "m"(*address), "0"(old_value)
		: "memory");

	return prev == old_value;
}

static inline unsigned int compare_and_swap128(volatile aba_128_t *address, aba_128_t old_value, aba_128_t new_value)
{

  //return  __sync_bool_compare_and_swap (address, old_value, new_value);
  /*
    inline bool cas( volatile types::uint128_t * src, types::uint128_t cmp, types::uint128_t with )

   bool result;
    __asm__ __volatile__
    (
        "lock cmpxchg16b oword ptr %1\n\t"
        "setz %0"
        : "=q" ( result )
        , "+m" ( *src )
        , "+d" ( cmp.hi )
        , "+a" ( cmp.lo )
        : "c" ( with.hi )
        , "b" ( with.lo )
        : "cc"
    );
    return result;
  */

   unsigned long result;
   asm volatile
    (
        "lock cmpxchg16b %1\n\t"
        "setz %0\n"
        : "=q" ( result )
        , "+m" ( *address )
        , "+d" ( old_value.ptr )
        , "+a" ( old_value.tag )
        : "c" ( new_value.ptr )
        , "b" ( new_value.tag )
        : "cc"
    );
    return result;



  /*
  unsigned long prev = 0;

  asm volatile(LOCK_PREFIX "cmpxchg16b %1,%2"
	       : "=a"(prev)
	       : "r"(new_value), "m"(*address), "0"(old_value)
	       : "memory");
  
  return prev == old_value;

  */
}

static inline unsigned long compare_and_swap_ptr(volatile void *address, void* old_ptr, void* new_ptr)
{
	return compare_and_swap64((volatile unsigned long *)address, (unsigned long)old_ptr, (unsigned long)new_ptr); 
}

#endif

