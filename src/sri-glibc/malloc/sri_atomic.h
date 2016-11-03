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

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdint.h>

#include "lfht.h"


/* only use lfht_entry_t to prevent breaking type-aliasing rules 
typedef struct u128_s {
  uint64_t uno;
  uint64_t dos;
} u128_t;
*/

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

static inline unsigned int cas_128(volatile lfht_entry_t *address, lfht_entry_t old_value, lfht_entry_t new_value)
{
  
  char result;
  asm volatile
    (LOCK_PREFIX "cmpxchg16b %1\n\t"
     "setz %0\n"
     : "=q" ( result )
       , "+m" ( *address )
     : "a" ( old_value.key ), "d" ( old_value.val )
       ,"b" ( new_value.key ), "c" ( new_value.val )
     : "cc"
     );
  return result;
}

#endif

