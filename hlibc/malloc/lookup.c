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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "gassert.h"
#include "lookup.h"
#include "lfht.h"

/* 
 * The sbrked regions of the main_arena.
 *
 * Note that only the main_arena futzes with these, and when it does
 * it has the main_arena lock. So we do not need to do anything
 * ourselves.
 *
 */
typedef struct sbrk_region_s {
  uintptr_t lo;
  uintptr_t hi;
  uintptr_t max;
} sbrk_region_t;

static int32_t sbrk_region_count = 0;
static sbrk_region_t sbrk_regions[1024];

/* 
 *  Just a placeholder to mark where we *really* should be using
 *  lfht_delete if we had it. Note that in our world 0 is an invalid
 *  value for either a heap index or the size of a mmapped region.
 */
#define TOMBSTONE 0

#define HEAP_HTABLE_CAPACITY 4096

#define MMAP_HTABLE_CAPACITY 4096

static lfht_t heap_tbl;  // maps heap ptr --> arena_index
static lfht_t mmap_tbl;  // maps mmapped region --> size 

/*
 * N.B. We could eliminate the need for a mmapped arena 
 * if we also stored the offset of the region, but ...
 */


void lookup_init(void){
  if( ! init_lfht(&heap_tbl, HEAP_HTABLE_CAPACITY) ||  
      ! init_lfht(&mmap_tbl, MMAP_HTABLE_CAPACITY)  ){
    fprintf(stderr, "Off to a bad start: lfht inits failed\n");
    abort();
  }
}

void lookup_delete(void){
  delete_lfht(&heap_tbl);
  delete_lfht(&mmap_tbl);
}


bool lookup_arena_index(void* ptr, size_t *arena_indexp){
  uintptr_t val = 0;
  bool success;
  uint32_t i;

  if(arena_indexp == NULL){
    return false;
  }

  for(i = 0; i <= sbrk_region_count; i++){
    if( sbrk_regions[i].lo <= (uintptr_t)ptr && 
	(uintptr_t)ptr < sbrk_regions[i].hi ){ 
      *arena_indexp = 1;
      return true;
    }
  }
  
  success = lfht_find(&mmap_tbl, (uintptr_t)ptr, &val);
  //fprintf(stderr, "lfht_find(&mmap_tbl, %p) = %d (val = %d)\n", ptr, success, (int)val);
  if(success && val != TOMBSTONE){
    *arena_indexp = 0;
    return true;
  }

  /* Compare with heap_for_ptr in arena.c */
  uintptr_t heap = ((unsigned long) (ptr) & ~(HEAP_MAX_SIZE - 1));

  success =  lfht_find(&heap_tbl, heap, &val);
  
  if(success && val != TOMBSTONE){
    *arena_indexp = (size_t)val;
    return true;
  }
  
  return 0;

}

bool lookup_add_sbrk_region(void* lo, void* hi){
  sbrk_region_count++;
  //FIXME!!
  assert(sbrk_region_count < 1024);

  sbrk_regions[sbrk_region_count].lo = (uintptr_t)lo;
  sbrk_regions[sbrk_region_count].hi = (uintptr_t)hi;
  sbrk_regions[sbrk_region_count].max = (uintptr_t)hi;

  return true;
}

bool lookup_set_sbrk_lo(void* ptr){
  assert(sbrk_region_count == 0);
  sbrk_regions[sbrk_region_count].lo = (uintptr_t) ptr;
  //fprintf(stderr, "sbrk_lo = %p\n", (void*)sbrk_lo);
  return true;
}

bool lookup_incr_sbrk_hi(size_t sz){
  if(sbrk_regions[sbrk_region_count].hi == 0){
    sbrk_regions[sbrk_region_count].hi = sbrk_regions[sbrk_region_count].lo;
  }
  sbrk_regions[sbrk_region_count].hi += sz;
  
  if(sbrk_regions[sbrk_region_count].max <  sbrk_regions[sbrk_region_count].hi){
    sbrk_regions[sbrk_region_count].max =  sbrk_regions[sbrk_region_count].hi;
  }
  
  //fprintf(stderr, "sbrk_hi = %p (delta = %zu)\n", (void*)sbrk_hi, sz);
  return true;
}

/* looks to be a bug in glibc. never do we trim mmapped sbrk mem */
bool lookup_decr_sbrk_hi(size_t sz){
  sbrk_regions[0].hi -= sz;
  //fprintf(stderr, "sbrk_hi = %p (delta = -%zu)\n", (void*)sbrk_hi, sz);
  return true;
}

bool lookup_add_heap(void* ptr, size_t index){
  bool retval = lfht_insert_or_update(&heap_tbl, (uintptr_t)ptr, (uintptr_t)index);
  //fprintf(stderr, "lookup_add_heap(%p, %zu) = %d\n", ptr, index, retval);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_delete_heap(void* ptr){
  bool retval = lfht_update(&heap_tbl, (uintptr_t)ptr, TOMBSTONE);
  //fprintf(stderr, "lookup_delete_heap(%p) = %d\n", ptr, retval);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_add_mmap(void* ptr, size_t sz){
  bool retval = lfht_insert_or_update(&mmap_tbl, (uintptr_t)ptr, (uintptr_t)sz);
  //fprintf(stderr, "lookup_add_mmap(%p, %zu) = %d\n", ptr, sz, retval);
  //lookup_dump(stderr);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_delete_mmap(void* ptr){
  bool retval = lfht_update(&mmap_tbl, (uintptr_t)ptr, TOMBSTONE);
  //fprintf(stderr, "lookup_delete_mmap(%p) = %d\n", ptr, retval);
  //lookup_dump(stderr);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

void lookup_dump(FILE* fp){
  uint32_t i;
  fprintf(fp, "lookup:\n");
  for(i = 0; i <= sbrk_region_count; i++){
    fprintf(fp, 
	    " sbrk[%d]: sbrk_lo = %p\tsbrk_hi = %p\tsbrk_max = %p\n", 
	    i, 
	    (void*)sbrk_regions[i].lo, 
	    (void*)sbrk_regions[i].hi, 
	    (void*)sbrk_regions[i].max);
  }
  lfht_stats(fp, " mmap_table", &mmap_tbl, TOMBSTONE);
  lfht_stats(fp, " heap_table", &heap_tbl, TOMBSTONE);
  fflush(fp);
}
