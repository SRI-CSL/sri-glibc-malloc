#include "memcxt.h"


#include <stdlib.h>
#include <string.h>


static void *default_allocate(memtype_t type, size_t size){
  void* mem;
  mem = malloc(size);
  memset(mem, 0, size);
  return mem;
}


static void default_release(memtype_t type, void *ptr, size_t ptr_sz){
  free(ptr);
}


memcxt_t default_memcxt = { default_allocate, default_release };


memcxt_t *sys_memcxt = &default_memcxt;


/* 
 * Add two size_t values, checking for overflow
 */
#if  __has_builtin(__builtin_add_overflow)

size_t add_size(size_t s1, size_t s2){
  size_t result;
  if( ! __builtin_add_overflow(s1, s2, &result) ){
    return result;
  } 
  abort();
}

#else

size_t add_size(size_t s1, size_t s2){
  size_t result;
  result = s1 + s2;
  if (result < s1 || result < s2){
    abort();
  }
  return result;
  }
#endif


/*
 * Multiply two size_t values, checking for overflow
 */

#if  __has_builtin(__builtin_mul_overflow)

size_t mul_size(size_t s1, size_t s2){
  size_t result;

  if( ! __builtin_mul_overflow(s1, s2, &result) ){
    return prod;
  } 

  abort();
}


#else

size_t mul_size(size_t s1, size_t s2){
  size_t result;
  if (s1 == 0 || s2 == 0){
    return 0;
  }
  result = s1 * s2;
  if (result / s2 != s1){
    abort();
  }
  return result;
}


#endif
