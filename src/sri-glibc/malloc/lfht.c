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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "lfht.h"
#include "utils.h"
#include "sri_atomic.h"

#define VERBOSE  false

/* migration tax rate.  */
#define MIGRATIONS_PER_ACCESS   3


/* 
 * Idea: might be better to not update ht->state and ht->hdr seperately,
 * but rather do it in one shot with a cas_64. ht->hdr is mmapped so 
 * page aligned so we can certainly steal the bits needed for the state.
 * Ian's question is should we do this with a bitfield hack, or a bit
 * mask in the lowest 4 bits.
 *
 */

/*
 * A table grows from N to 2N when there are N/R non-zero keys, where
 * R is the RESIZE_RATIO.  The new table, before it needs to grow, has
 * 2N/R free slots. The N/R key-values in the old table need to be
 * migrated before that happens. So, worst case, in N/R more
 * insertions it will need to grow.  Assuming, the worst case, where
 * there are no TOMBSTONEs anywhere. Thus the tax rate T must be such
 * that N/R * T > N/R.
 *
 *  So a tax rate > 1 will suffice. We have chosen 3.
 *
 */


/* 
 *
 * A key is marked as assimilated to indicate that the key-value pair
 * has been moved from the table it is in, to the current active
 * table.
 *
 * When all the key-value pairs in a table are marked as assimilated,
 * then the table_hdr itself is marked as assimilated.
 * 
 * Slow threads present a nuisance here. They should check after
 * completeing an operation, that they were not operating on a
 * assimilated table. If they were, then they need to repeat the
 * operation. Termination then becomes an issue.
 *
 */

#define ASSIMILATED 0x1

static inline bool is_assimilated(uint64_t key){
  return (key & (KEY_ALIGNMENT - 1)) != 0;
}

static inline uint64_t set_assimilated(uint64_t key){
  return (key | ASSIMILATED);
}

bool free_lfht_hdr(lfht_hdr_t *hdr){
  if (hdr != NULL){
    int retcode = munmap(hdr, hdr->sz);
    if (retcode != 0){
      return false;
    }
  }
  return true;
}

lfht_hdr_t *alloc_lfht_hdr(uint32_t max){
  uint64_t sz;
  void *addr;
  
  sz = (max * sizeof(lfht_entry_t)) + sizeof(lfht_hdr_t);

  addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    
  if (addr != MAP_FAILED) {
    lfht_hdr_t *hdr = (lfht_hdr_t *)addr;
      atomic_init(&hdr->assimilated, false);
      hdr->sz = sz;
      hdr->max = max;
      hdr->threshold = (uint32_t)(max * RESIZE_RATIO);
      atomic_init(&hdr->count, 0);
      hdr->next = NULL;
      hdr->table = addr + sizeof(lfht_hdr_t);
      return hdr;
  } else {
    return NULL;
  }
}

bool init_lfht(lfht_t *ht, uint32_t max){
  lfht_hdr_t *hdr;
  if (ht != NULL && max != 0){
    hdr = alloc_lfht_hdr(max);
    if (hdr != NULL){
      atomic_init(&ht->state, INITIAL);
      ht->table_hdr = hdr;
      return true;
    } 
  }
  return false;
}

bool delete_lfht(lfht_t *ht){
  lfht_hdr_t *hdr;
  
  if (ht != NULL){

    hdr = (lfht_hdr_t *)ht->table_hdr;
    
    while(hdr != NULL){
      lfht_hdr_t * next = hdr->next;
      bool success = free_lfht_hdr(hdr);
      assert(success);
      ht->table_hdr = NULL;
      hdr = next;
    }
    return true;
  }
  
  return false;
}

/* returns true if this attempt succeeded; false otherwise. */
bool _grow_table(lfht_t *ht,  lfht_hdr_t *hdr){

  lfht_hdr_t *ohdr = (lfht_hdr_t *)ht->table_hdr;
  
  /* someone beat us to it */
  if(hdr != ohdr){
    if(VERBOSE){ fprintf(stderr, "LOST RACE: ht->state = %d\n", ht->state); }
    return false;
  }
  
  uint32_t omax = ohdr->max;

  if (omax < MAX_TABLE_SIZE){ 

    uint32_t nmax = 2 * ohdr->max;

    lfht_hdr_t *nhdr  = alloc_lfht_hdr(nmax);
  
    if (nhdr != NULL){
      nhdr->next = ohdr;

      if (cas_64((volatile uint64_t *)&(ht->table_hdr), (uint64_t)ohdr, (uint64_t)nhdr)){
	/* we succeeded in adding the new table */
	assert(atomic_load(&ht->state) == INITIAL || atomic_load(&ht->state) == EXPANDED);

	atomic_store(&ht->state, EXPANDING);
	
	assert(ohdr->next == NULL || ohdr->next->assimilated);

	assert(ht->table_hdr->next == hdr);

	return true;
      } else {
	free_lfht_hdr(nhdr);
	return false;
      }
    }
  }
  
  return false;
}

static uint32_t assimilate(lfht_t *ht, lfht_hdr_t *from_hdr, uint64_t key, uint32_t hash,  uint32_t count);

static inline void _migrate_table(lfht_t *ht, uint64_t key, uint32_t hash){
  unsigned int table_state = atomic_load(&ht->state);

  if (table_state == EXPANDING){
    /* gotta pitch in and do some migrating */
    lfht_hdr_t *hdr = (lfht_hdr_t *)ht->table_hdr;
    lfht_hdr_t *ohdr = hdr->next;

    uint32_t moved = assimilate(ht, ohdr, key, hash,  MIGRATIONS_PER_ACCESS);

    if (moved <  MIGRATIONS_PER_ACCESS){
      /* the move has finished! */
      atomic_store(&ohdr->assimilated, true);

      /* someone may have triggered another expansion (we could be slow) */
      if(ht->table_hdr == hdr){
	atomic_store(&ht->state, EXPANDED);
      } else {
	if(VERBOSE){ fprintf(stderr, "Table expanding, not marking as expanded\n");}
      }
      /* 
	 note that we never free the old table. To do so we would have to do more work
	 like maintaining a reference count.
      */

    }

  }
}


static bool _lfht_add(lfht_t *ht, uint64_t key, uint64_t val, bool external){
  uint32_t hash, mask, j, i, retries;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  bool retval;

  retval = false;
  retries = 0;

  assert( key != 0 );
  assert( ! is_assimilated(key) );
  assert( val != TOMBSTONE );

  if (ht == NULL  || ht->table_hdr == NULL || key == 0  || val == TOMBSTONE){
    return retval;
  }
  
  hash = jenkins_hash_ptr((void *)key);

  /* only external calls pay the migration tax */
  if ( external ){ _migrate_table(ht, key, hash); }

 retry:
  
  hdr = (lfht_hdr_t *)ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;

  
  j = hash & mask;
  i = j;
  
  while (true) {

    entry = table[i];
    
    if (entry.key == 0){
      if (cas_64((volatile uint64_t *)&(table[i].key), entry.key, key)){
	//iam: discuss
	table[i].val = val;
	
	const uint_least32_t count = atomic_fetch_add(&hdr->count, 1);
	
	if (count + 1 > hdr->threshold){
	  _grow_table(ht, hdr);
	}
	retval = true;
	goto exit;
      } else {
	continue;
      }
    }
    
    if (entry.key == key){
      if (cas_64((volatile uint64_t *)&(table[i].val), entry.val, val)){
	retval = true;
	goto exit;
      } else {
	continue;
      }
    }
    
    i++;
    i &= mask;
    
    if ( i == j ){ break; }
    
  }

 exit:

  /* what do we do if we notice hdr != ht->table_hdr at this point? ian thinks thats fine, think serialization. */

  /* slow thread last gasp */
  if ( external && atomic_load(&hdr->assimilated)){
    /* could have a fail count */
    if(VERBOSE){ fprintf(stderr, "lfht_add: RETRYING %"PRIu32"\n", retries); }
    retries++;
    goto retry;
  }

  
  return retval;
}


bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val){
  return _lfht_add(ht, key, val, true);
}


bool lfht_remove(lfht_t *ht, uint64_t key){
  uint32_t hash, mask, j, i, retries;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  bool retval;

  retval = false;
  retries = 0;

  if (ht == NULL || ht->table_hdr == NULL || key == 0){
    return retval;
  }

  hash = jenkins_hash_ptr((void *)key);

  _migrate_table(ht, key, hash);
  

 retry:
  
  hdr = (lfht_hdr_t *)ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;

  j = hash & mask;
  i = j;
  
  
  while (true) {
    
    entry = table[i];
    
    if (entry.key == key){
      if (cas_64((volatile uint64_t *)&(table[i].val), entry.val, TOMBSTONE)){
	retval =true;
	goto exit;
      } else {
	continue;
      }
    } else if (entry.key == 0){
      goto exit;
    }
    
    i++;
    i &= mask;
      
    if ( i == j ){ break; }

  }
  
 exit:

  /* what do we do if we notice hdr != ht->table_hdr at this point? ian thinks thats fine, think serialization. */

  /* slow thread last gasp */
  if (atomic_load(&hdr->assimilated)){
    /* could have a fail count */
    if(VERBOSE){ fprintf(stderr, "lfht_remove: RETRYING %"PRIu32"\n", retries); }
    retries++;
    goto retry;
  }


  return retval;
}


bool lfht_find(lfht_t *ht, uint64_t key, uint64_t *valp){
  uint32_t hash, mask, j, i, retries;
  uint64_t kval;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  bool retval;

  retval = false;
  retries = 0;
    
  if (ht == NULL || ht->table_hdr == NULL || key == 0 || valp == NULL){
    return retval;
  }

  hash = jenkins_hash_ptr((void *)key);

  _migrate_table(ht, key, hash);

 retry:
  
  hdr = (lfht_hdr_t *)ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;
  
  
  j = hash & mask;
  i = j;
  
  
  while (true) {
    
    kval = read_64((volatile uint64_t *)&table[i].key);
    
    if (kval == 0){
      goto exit;
    }
    
    if (kval == key){
      *valp = table[i].val;
      retval = true;
      goto exit;
    }
    
    i++;
    i &= mask;
    
    if ( i == j ){ break; }
    
  }
  
 exit:

  /* what do we do if we notice hdr != ht->table_hdr at this point? ian thinks thats fine, think serialization. */

  /* slow thread last gasp */
  if (atomic_load(&hdr->assimilated)){  
    /* could have a fail count */
    if(VERBOSE){ fprintf(stderr, "lfht_find: RETRYING %"PRIu32"\n", retries); }
    retries++;
    goto retry;
  }

  
  return retval;
}

/*
 * The migration tax. 
 * 
 * The thread attempts to move at least count key-value pairs from the
 * old table (from_hdr) to the new table (ht->table_hdr). It starts
 * the job where the key of interest may lie. It also makes sure that
 * the key of interest, if it has a non-TOMBSTONE value in the table,
 * has been moved.  Thus after paying the migration tax, the operation
 * can concentrate on the current table to service the request.
 *
 * Drew says we could think about doing this in a cache friendly fashion.
 *
 */

static uint32_t assimilate(lfht_t *ht, lfht_hdr_t *from_hdr, uint64_t key, uint32_t hash,  uint32_t count){
  uint32_t retval, mask, j, i;
  lfht_entry_t entry;
  uint64_t akey, dkey;
  lfht_entry_t*  table;
  bool success, moveit;

  /* indicates that we are sure the key is either not in the table, or has been assimilated */
  success = false;
  moveit = false;
  
  dkey = set_assimilated(key);
  retval = 0;
  mask = from_hdr->max - 1;
  table = from_hdr->table;
  
  if (atomic_load(&from_hdr->assimilated)){
    return retval;
  }

  j = hash & mask;
  i = j;

  while (true) {

    entry = table[i];

    if (entry.key == 0 || entry.key == dkey) {
      success = true;
    } else if (entry.key == key){
      success = true;
      moveit = true;
    } 

    if ( moveit || retval < count ){
      if ( moveit ){  moveit = false; }
      if ( entry.key && ! is_assimilated(entry.key) ){
	akey = set_assimilated(entry.key);
	if (cas_64((volatile uint64_t *)&(table[i].key), entry.key, akey)){
	  if (entry.val != TOMBSTONE){
	    _lfht_add(ht, entry.key, entry.val, false);
	    retval ++;
	  }
	}
      }
    } else {
      if (success && retval >= count){ break; }
    }
    
    i++;
    i &= mask;
    
    if ( i == j ){ break; }
  }
  
  return retval;
}



void lfht_hdr_dump(FILE* fp, lfht_hdr_t *hdr, uint32_t index){
  uint32_t i, max, count, moved, tombstoned;
  lfht_entry_t*  table;
  lfht_entry_t  entry;

  max = hdr->max;
  count = 0;
  moved = 0;
  tombstoned = 0;
  table = hdr->table;
  
  for(i = 0; i < max; i++){

    entry = table[i];
    
    
    if(entry.key != 0){

      count++;

      if(entry.val == TOMBSTONE){ tombstoned++; }

      if( is_assimilated(entry.key) ){ moved++; }

    }
  }
  
  fprintf(fp, "\ttable[%"PRIu32"]: assimilated = %d max = %"PRIu32\
	  " count =  %"PRIu32" threshold =  %"PRIu32" actual =  %"PRIu32\
	  " moved =  %"PRIu32" tombstoned =  %"PRIu32"\n",
	  index, hdr->assimilated, hdr->max, hdr->count, hdr->threshold,
	  count, moved, tombstoned);
}

void lfht_stats(FILE* fp, const char* name, lfht_t *ht){
  uint32_t index;
  lfht_hdr_t *hdr;
  
  index = 0;
  hdr = (lfht_hdr_t *)ht->table_hdr;
  fprintf(fp, "%s table state: %u\n", name, ht->state);
  while(hdr != NULL){
    lfht_hdr_dump(fp, hdr, index);
    hdr = hdr->next;
    index++;
  }
}


void lfht_dump(FILE* fp, const char* name, lfht_t *ht){
  uint32_t i, max;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t  entry;

  hdr = (lfht_hdr_t *)ht->table_hdr;
  max = hdr->max;
  table = hdr->table;

  fprintf(fp, "%s table contents:\n", name);

  for(i = 0; i < max; i++){
    entry = table[i];
    if(entry.key != 0){
      fprintf(stderr, "\t%p => %"PRIu64"\n", (void*)entry.key, entry.val);
    }
  }
}
