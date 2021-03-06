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
#include <string.h>

#include "gassert.h"
#include "lookup.h"
#include "lfht.h"
#include "utils.h"




/* 
 * The sbrked regions of the main_arena.
 *
 * Note that only the main_arena futzes with these, and when it does
 * it has the main_arena lock. So we do not need to do anything
 * ourselves. Though lots of threads might be reading them, so
 * we may need to make them atomic(ish)
 *
 * To eliminate racish things we probably want to:
 * 
 * split out the actual sbrk region sbrk_regions[0] from the mmapped
 * ones (which do not grow so do not need a max) and make them atomic.
 *
 * if we are careful with the array of mmapped ones we probably only need
 * to make the count atomic.
 *
 */

#define SBRK_INITIAL_MAX_SEGMENTS 1024

/* (1 << 31) or 2^31  */
#define MAX_SBRK_TABLE_SIZE ((uint32_t)0x80000000u)

/* Max is just for curiosity. */
typedef struct sbrk_region_s {
  bool mmapped;
  uintptr_t lo;
  uintptr_t hi;
  uintptr_t max;
} sbrk_region_t;


static int32_t sbrk_region_count = 0;
static int32_t sbrk_region_current_max = SBRK_INITIAL_MAX_SEGMENTS;

static sbrk_region_t *sbrk_regions;

/* 
 *  Note that in our world 0 is an invalid value for either a heap
 *  index or the size of a mmapped region.
 */
#define TOMBSTONE 0

#define TABLE_SIZE  1024
//64
//1024
//4096

#define HEAP_HTABLE_CAPACITY TABLE_SIZE

#define MMAP_HTABLE_CAPACITY TABLE_SIZE


static lfht_t heap_tbl;  // maps heap ptr --> arena_index
static lfht_t mmap_tbl;  // maps mmapped region --> size 
static size_t heap_max;  // value of HEAP_MAX_SIZE at runtime
/*
 * N.B. We could eliminate the need for a mmapped arena 
 * if we also stored the offset of the region, somehow ...
 */


void lookup_init(size_t hmax){
  heap_max = hmax;
  sbrk_regions = sri_mmap(NULL, sbrk_region_current_max * sizeof(sbrk_region_t));
  if( ! sbrk_regions ||
      ! init_lfht(&heap_tbl, HEAP_HTABLE_CAPACITY) ||  
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
  if(success && val != TOMBSTONE){
    *arena_indexp = 0;
    return true;
  }

  /* Compare with heap_for_ptr in arena.c */
  uintptr_t heap = ((unsigned long) (ptr) & ~(heap_max - 1));

  success =  lfht_find(&heap_tbl, heap, &val);
  
  if(success && val != TOMBSTONE){
    *arena_indexp = (size_t)val;
    return true;
  }
  
  return false;

}

bool lookup_add_sbrk_region(void* lo, void* hi){
  int nregion_count = sbrk_region_count + 1;

  if(nregion_count == sbrk_region_current_max){
    /* need to grow the sbrk_regions array */
    if(sbrk_region_current_max >= MAX_SBRK_TABLE_SIZE){ 
      fprintf(stderr, "lookup_add_sbrk_region failed. Too big, but not too big to fail.");
      return false;
    } else {

      uint32_t nregion_max = 2 * sbrk_region_current_max;
      sbrk_region_t *nregions = sri_mmap(sbrk_regions, nregion_max * sizeof(sbrk_region_t));
      if(nregions != sbrk_regions){
	memcpy(nregions, sbrk_regions, sbrk_region_current_max * sizeof(sbrk_region_t));
	sbrk_regions = nregions;
      }
      sbrk_region_current_max = nregion_max;
    }
  }

  sbrk_regions[nregion_count].mmapped = true;
  sbrk_regions[nregion_count].lo = (uintptr_t)lo;
  sbrk_regions[nregion_count].hi = (uintptr_t)hi;
  sbrk_regions[nregion_count].max = (uintptr_t)hi;

  sbrk_region_count = nregion_count;

  return true;
}

bool lookup_set_sbrk_lo(void* ptr){
  sbrk_regions[0].lo = (uintptr_t) ptr;
  return true;
}

bool lookup_incr_sbrk_hi(size_t sz){
  if(sbrk_regions[0].hi == 0){
    sbrk_regions[0].hi = sbrk_regions[0].lo;
  }
  sbrk_regions[0].hi += sz;
  
  if(sbrk_regions[0].max <  sbrk_regions[0].hi){
    sbrk_regions[0].max =  sbrk_regions[0].hi;
  }
  
  return true;
}

/* looks to be a bug in glibc. never do we trim mmapped sbrk mem */
bool lookup_decr_sbrk_hi(size_t sz){
  sbrk_regions[0].hi -= sz;
  return true;
}

bool lookup_add_heap(void* ptr, size_t index){
  bool retval = lfht_add(&heap_tbl, (uintptr_t)ptr, (uintptr_t)index);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_delete_heap(void* ptr){
  bool retval = lfht_remove(&heap_tbl, (uintptr_t)ptr);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_add_mmap(void* ptr, size_t sz){
  bool retval = lfht_add(&mmap_tbl, (uintptr_t)ptr, (uintptr_t)sz);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

bool lookup_delete_mmap(void* ptr){
  bool retval = lfht_remove(&mmap_tbl, (uintptr_t)ptr);
  assert(retval);
  if(!retval){ abort(); }
  return retval;
}

void lookup_dump(FILE* fp, bool dumptables){
  uint32_t i;
  fprintf(fp, "lookup:\n");
  for(i = 0; i <= sbrk_region_count; i++){
    fprintf(fp, 
	    "\tsbrk[%d]: sbrk_lo = %p\tsbrk_hi = %p\tsbrk_max = %p  mmapped = %d\n", 
	    i, 
	    (void*)sbrk_regions[i].lo, 
	    (void*)sbrk_regions[i].hi, 
	    (void*)sbrk_regions[i].max,
	    sbrk_regions[i].mmapped
	    );
  }
  lfht_stats(fp, " mmap_table", &mmap_tbl);
  lfht_stats(fp, " heap_table", &heap_tbl);
  if(dumptables){
    lfht_dump(fp,  " mmap_table", &mmap_tbl);
    lfht_dump(fp,  " heap_table", &mmap_tbl);
  }
  fflush(fp);
}
