#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"


#define MIGRATIONS_PER_ACCESS   3

/* 
 *
 * A key is marked as assimilated to indicate that
 * the key-value pair has been moved from the table it is in,
 * to the current active table. 
 *
 * When all the key-value pairs in a table are marked as assimilated,
 * the the table_hdr itself is marked as assimilated. 
 * 
 * Slow threads present a nuisance here.
 *
 */

#define ASSIMILATED 0x1

static inline bool is_assimilated(uint64_t key){
  return (key & (KEY_ALIGNMENT - 1)) != 0;
}

static inline uint64_t set_assimilated(uint64_t key){
  return (key | ASSIMILATED);
}

/*
static inline uint64_t clear_assimilated(uint64_t key){
  return (key & ~ASSIMILATED);
}
*/

bool free_lfht_hdr(lfht_tbl_hdr_t *hdr){
  if(hdr != NULL){
    int retcode = munmap(hdr, hdr->sz);
    if(retcode != 0){
      return false;
    }
  }
  return true;
}

lfht_tbl_hdr_t *alloc_lfht_hdr(uint32_t max){
  uint64_t sz;
  void *addr;
  
  sz = (max * sizeof(lfht_entry_t)) + sizeof(lfht_tbl_hdr_t);

  addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    
  if (addr != MAP_FAILED) {
    lfht_tbl_hdr_t *hdr = (lfht_tbl_hdr_t *)addr;
      atomic_init(&hdr->assimilated, false);
      hdr->sz = sz;
      hdr->max = max;
      hdr->threshold = (uint32_t)(max * RESIZE_RATIO);
      atomic_init(&hdr->count, 0);
      hdr->next = NULL;
      hdr->table = addr + sizeof(lfht_tbl_hdr_t);
      return hdr;
  } else {
    return NULL;
  }
}

bool init_lfht(lfht_t *ht, uint32_t max){
  lfht_tbl_hdr_t *hdr;
  if(ht != NULL && max != 0){
    hdr = alloc_lfht_hdr(max);
    if(hdr != NULL){
      atomic_init(&ht->state, INITIAL);
      ht->table_hdr = hdr;
      return true;
    } 
  }
  return false;
}

bool delete_lfht(lfht_t *ht){
  lfht_tbl_hdr_t *hdr;
  
  if(ht != NULL){

    hdr = ht->table_hdr;
    
    while(hdr != NULL){
      lfht_tbl_hdr_t * next = hdr->next;
      bool success = free_lfht_hdr(hdr);
      assert(success);
      ht->table_hdr = NULL;
      hdr = next;
    }

    ht->state = DELETED;
    return true;
  }
  
  return false;
}

/* returns true if this attempt succeeded; false otherwise. */
bool _grow_table(lfht_t *ht){

  lfht_tbl_hdr_t *ohdr = ht->table_hdr;
  uint32_t omax = ohdr->max;

  if(omax < MAX_TABLE_SIZE){ 

    uint32_t nmax = 2 * ohdr->max;

    lfht_tbl_hdr_t *nhdr  = alloc_lfht_hdr(nmax);
  
    if(nhdr != NULL){
      nhdr->next = ohdr;

      if(cas_64((volatile uint64_t *)&(ht->table_hdr), (uint64_t)ohdr, (uint64_t)nhdr)){
	assert(atomic_load(&ht->state) == INITIAL || atomic_load(&ht->state) == EXPANDED);
	atomic_store(&ht->state, EXPANDING);
	return true;
      } else {
	free_lfht_hdr(nhdr);
	return false;
      }
    }
  }
  
  return false;
}

static uint32_t assimilate(lfht_t *ht, lfht_tbl_hdr_t *from_hdr, uint32_t hash,  uint32_t count);

static inline void _migrate_table(lfht_t *ht, lfht_tbl_hdr_t *hdr, uint32_t hash){
  unsigned int table_state = atomic_load(&ht->state);

  if(table_state == EXPANDING){
    /* gotta pitch in and do some migrating */
    
    uint32_t moved = assimilate(ht, hdr->next, hash,  MIGRATIONS_PER_ACCESS);
    if(moved != MIGRATIONS_PER_ACCESS){
      /* the move has finished! */
      atomic_store(&hdr->next->assimilated, true);
      atomic_store(&ht->state, EXPANDED);
    }
  }
}


static bool _lfht_add(lfht_t *ht, uint64_t key, uint64_t val, bool external){
  uint32_t hash, mask, j, i;
  lfht_tbl_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  
  assert( ! is_assimilated(key) );
  assert(val != TOMBSTONE);

  if(ht != NULL  && ht->table_hdr != NULL  && key != 0  && val != TOMBSTONE){
    hdr = ht->table_hdr;
    table = hdr->table;
    mask = hdr->max - 1;
    hash = jenkins_hash_ptr((void *)key);

    /* only external calls pay the migration tax */
    if( external ){ _migrate_table(ht, hdr, hash); }

    j = hash & mask;
    i = j;

    while (true) {

      entry = table[i];

      if(entry.key == 0){
	if(cas_64((volatile uint64_t *)&(table[i].key), entry.key, key)){
	  //iam: discuss
	  table[i].val = val;

	  const uint_least32_t count = atomic_fetch_add(&hdr->count, 1);

	  if(count + 1 > hdr->threshold){
	    _grow_table(ht);
	  }
	  
	  
	  return true;
	} else {
	  continue;
	}
      }
      
      if(entry.key == key){
	if(cas_64((volatile uint64_t *)&(table[i].val), entry.val, val)){
	  return true;
	} else {
	  continue;
	}
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }

    }
  }
  return false;
}


bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val){
  return _lfht_add(ht, key, val, true);
}


bool lfht_remove(lfht_t *ht, uint64_t key){
  uint32_t hash, mask, j, i;
  lfht_tbl_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  
  if(ht != NULL  && ht->table_hdr != NULL  && key != 0){

    hdr = ht->table_hdr;
    table = hdr->table;
    mask = hdr->max - 1;
    hash = jenkins_hash_ptr((void *)key);

    _migrate_table(ht, hdr, hash);

    j = hash & mask;
    i = j;

    
    while (true) {

      entry = table[i];

      if(entry.key == key){
	if(cas_64((volatile uint64_t *)&(table[i].val), entry.val, TOMBSTONE)){
	  return true;
	} else {
	  continue;
	}
      } else if(entry.key == 0){
	return false;
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }

    }

  }
	
  return false;

}


bool lfht_find(lfht_t *ht, uint64_t key, uint64_t *valp){
  uint32_t hash, mask, j, i;
  uint64_t kval;
  lfht_tbl_hdr_t *hdr;
  lfht_entry_t*  table;
    
  if(ht != NULL  && ht->table_hdr != NULL  && key != 0 && valp != NULL){

    hdr = ht->table_hdr;
    table = hdr->table;
    mask = hdr->max - 1;
    hash = jenkins_hash_ptr((void *)key);

    _migrate_table(ht, hdr, hash);

    j = hash & mask;
    i = j;
  

    while (true) {

      kval = read_64((volatile uint64_t *)&table[i].key);

      if(kval == 0){
	return false;
      }

      if(kval == key){
	*valp = table[i].val;
	return true;
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }
      
    }
  }

  return false;
}
  
/*
 * The migration tax. Attempts to move count key-value pairs from the old table (from_hdr)
 * to the new table (ht->table_hdr). It starts the job where the key of interest may lie. It might
 * make sense for it to also handle the actual key of interest is one way or another.
 *
 * Discuss.
 *
 */

static uint32_t assimilate(lfht_t *ht, lfht_tbl_hdr_t *from_hdr, uint32_t hash,  uint32_t count){
  uint32_t retval, mask, j, i;
  lfht_entry_t entry;
  uint64_t akey;
  lfht_entry_t*  table;

  retval = 0;
  mask = from_hdr->max - 1;
  table = from_hdr->table;
  
  if(atomic_load(&from_hdr->assimilated)){
    return retval;
  }

  j = hash & mask;
  i = j;
  

  while (retval < count) {

    entry = table[i];

    if( ! is_assimilated(entry.key) ){
      akey = set_assimilated(entry.key);
      if(cas_64((volatile uint64_t *)&(table[i].key), akey, entry.key)){
	_lfht_add(ht, entry.key, entry.val, false);
	retval ++;
      }
    }

    
    i++;
    i &= mask;
    
    if( i == j ){ break; }
    
    
  }
  
  return count;
}
