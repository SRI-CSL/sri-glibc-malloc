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
#include <assert.h>
#include <sys/mman.h>

#include "malloc_internals.h"
#include "util.h"

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

  a = (uint32_t) x;         // low order bits
  b = (uint32_t) (x >> 32); // high order bits
  c = 0xdeadbeef;
  final(a, b, c);

  return c;
}


/*
 * BD's: Hash code for an arbitrary pointer p
 */
uint32_t jenkins_hash_ptr(const void *p) {
  return jenkins_hash_uint64((uint64_t)p);
}


static inline void *__mmap(void *addr, size_t length, int prot, int flags)
{
  return mmap(addr, length, prot, flags|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
}


void* aligned_mmap(size_t size, size_t alignment)
{
  void* addr;
  char *p1, *p2;
  uint64_t ul;

  assert( alignment == 0 ||  is_power_of_two(alignment));

  assert( alignment == 0 || alignment == size );

  assert( alignment == 0 || size % 16 == 0);


  size = align_up(size, PAGESIZE);

  /* just a plain mmap if we have no alignment constraints */
  if( ! alignment ) {
    
    assert(size % 16 == 0);

    addr = __mmap(0, size, PROT_READ | PROT_WRITE, 0);

    return addr == MAP_FAILED ? NULL : addr;

  } else {
    
    p1 = (char *) __mmap (0, alignment << 1, PROT_NONE, MAP_NORESERVE);
    if (p1 != MAP_FAILED)
      {
	p2 = (char *) (((uintptr_t) p1 + ((uintptr_t)alignment - 1)) & ~((uintptr_t)alignment - 1));
	ul = p2 - p1;
	if (ul){
	  munmap (p1, ul);
	} else {
	  munmap (p2 + alignment, alignment - ul);
        }
      }
    else
      {
	/* Maybe an allocation of only 'alignment' bytes is already aligned. */
	p2 = (char *) __mmap (0, alignment, PROT_NONE, MAP_NORESERVE);
	if (p2 == MAP_FAILED){
	  return 0;
	} else {
	
	if ((uint64_t) p2 & (alignment - 1))
	  {
	    munmap (p2, alignment);
	    return 0;
	  }
	}
      }
  }
  if (mprotect (p2, alignment, PROT_READ | PROT_WRITE) != 0)
    {
      munmap (p2, alignment);
      return 0;
    }
  
  

  return p2;

} 
