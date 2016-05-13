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
#ifndef __UTIL_H__
#define __UTIL_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

static inline void atomic_increment(atomic_ulong *counter){
  atomic_fetch_add(counter, 1);
}

static inline void atomic_add(atomic_ulong *counter, atomic_ulong incr){
  atomic_fetch_add(counter, incr);
}

static inline void atomic_decrement(atomic_ulong *counter){
  atomic_fetch_sub(counter, 1);
}

static inline void atomic_sub(atomic_ulong *counter, atomic_ulong decr){
  atomic_fetch_sub(counter, decr);
}


static inline long min(long a, long b)
{
  return a < b ? a : b;
}

static inline long max(long a, long b)
{
  return a > b ? a : b;
}


/* for sanity checking */
static inline bool is_power_of_two(uint64_t n)
{
  return (n & (n - 1)) == 0;
}

/* Fast modulo arithmetic, assuming that y is a power of 2 */
static inline size_t mod_power_of_two(uint64_t x, uint64_t y)
{
  assert(is_power_of_two(y));
  return x & (y - 1);
}

static inline size_t align_up(size_t value, size_t align)
{
  assert(is_power_of_two(align));
  return (value + (align - 1)) & ~(align - 1);
}


extern uint32_t jenkins_hash_uint64(uint64_t x);

extern uint32_t jenkins_hash_ptr(const void *p);

/*
  mmaps a region of size sz. if alignment is
  not zero, then it must be:
  - the same as sz
  - a power of two
  
  if alignment is 0, then sz is rounded up to 
  a multiple of PAGESIZE.

*/

void* aligned_mmap(size_t sz, size_t alignment);



#endif


