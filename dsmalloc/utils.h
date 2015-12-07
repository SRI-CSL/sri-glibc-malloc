#ifndef _HASHFNS_H
#define _HASHFNS_H

#if 0
#include <assert.h>
#else
#include "dsassert.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


/*
 *  Bruno Dutertre's versions of Jenkin's hash functions.
 *
 */

extern uint32_t jenkins_hash_uint64(uint64_t x);

extern uint32_t jenkins_hash_ptr(const void *p);


/* routines for size_t multiplication and size_t addition that detect overflow */

extern bool add_size(size_t s1, size_t s2, size_t* sum);

extern bool mul_size(size_t s1, size_t s2, size_t* prod);

/* Also courtesy of Bruno Dutertre 
 *
 * uint32_t ctz64(uint64_t x):
 * - return the index (between 0 and 63) of the lowest-order bit
 *   of x that's not 0
 * - x must be nonzero
 */

#ifdef __GNUC__

static inline uint32_t ctz64(uint64_t x) {
  assert(x != 0);
  return __builtin_ctzl(x);
}

#else

static inline uint32_t ctz64(uint64_t x) {
  uint64_t m;
  uint32_t i;

  assert(x != 0);
  m = 1;
  i = 0;
  while ((x & m) == 0) {
    i ++;
    m += m;
  }
  return i;
}

#endif


#endif
