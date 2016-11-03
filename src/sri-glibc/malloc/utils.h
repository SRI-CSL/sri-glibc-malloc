/*
 * Copyright (C) 2016  SRI International
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _HASHFNS_H
#define _HASHFNS_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "gassert.h"

#ifndef NDEBUG
extern bool is_power_of_two(uint32_t n);
#endif

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

extern void* sri_mmap(void* oldaddr, size_t size);


extern bool sri_munmap(void* memory, size_t size);

#endif
