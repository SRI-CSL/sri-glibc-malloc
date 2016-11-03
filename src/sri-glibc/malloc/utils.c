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
#include "utils.h"

#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>

/* for sanity checking */
#ifndef NDEBUG
bool is_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}
#endif


/* 
 * BD's Jenkins's lookup3 code 
 */

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define final(a,b,c)      \
{                         \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/*
 * BD's: Hash code for a 64bit integer
 */
uint32_t jenkins_hash_uint64(uint64_t x) {
  uint32_t a, b, c;

  a = (uint32_t) x; // low order bits
  b = (uint32_t) (x >> 32); // high order bits
  c = 0xdeadbeef;
  final(a, b, c);

  return c;
}


/*
 * BD's: Hash code for an arbitrary pointer p
 */
uint32_t jenkins_hash_ptr(const void *p) {
  return jenkins_hash_uint64((uint64_t) ((size_t) p));
}




/* Add two size_t values, checking for overflow */
bool add_size(size_t s1, size_t s2, size_t* sum){
  size_t result;
  
  assert(sum != NULL);

  result = s1 + s2;
  if (result < s1 || result < s2){
    return false;
  }

  *sum = result;
  return true;

}


/* Multiply two size_t values, checking for overflow */
bool mul_size(size_t s1, size_t s2, size_t* prod){
  size_t result;

  assert(prod != NULL);

  if (s1 == 0 || s2 == 0){
    *prod = 0;
    return true;
  }
  result = s1 * s2;
  if (result / s2 != s1){
    return false;
  }
  
  *prod = result;
  return result;
}


void* sri_mmap(void* oldaddr, size_t size){
  void* memory;
  int flags;
  int protection;

  /* beef this up later  */

  protection = PROT_READ | PROT_WRITE;
  flags = MAP_PRIVATE | MAP_ANON;

  /* try extending the current region */
  memory = mmap(oldaddr, size, protection, flags, -1, 0);

  /* if extending fails, then just try and map a new one */
  if((oldaddr != NULL) && (memory == MAP_FAILED)){
    memory = mmap(0, size, protection, flags, -1, 0);
  }
  
  if(memory == MAP_FAILED){
    memory = NULL;
  }

  return memory;
}

bool sri_munmap(void* memory, size_t size){
  int rcode;

  rcode = munmap(memory, size);
  
  return rcode != -1;
}
