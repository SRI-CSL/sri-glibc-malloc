#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"

/* migration tax rate.  */
#define MIGRATIONS_PER_ACCESS   3

/*
 * A table grows from N to 2N when there are N/R non-zero keys.  The
 * new table, before it needs to grow, has 2N/R free slots. So in N/R
 * more insertions it will need to grow, assuming the worst case where
 * there are no TOMBSTONEs. Thus the tax rate T must be such that N/R
 * * T > N/R.
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

/*
static inline uint64_t clear_assimilated(uint64_t key){
  return (key & ~ASSIMILATED);
}
*/

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

    hdr = ht->table_hdr;
    
    while(hdr != NULL){
      lfht_hdr_t * next = hdr->next;
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

  lfht_hdr_t *ohdr = ht->table_hdr;
  uint32_t omax = ohdr->max;

  if (omax < MAX_TABLE_SIZE){ 

    uint32_t nmax = 2 * ohdr->max;

    lfht_hdr_t *nhdr  = alloc_lfht_hdr(nmax);
  
    if (nhdr != NULL){
      nhdr->next = ohdr;

      if (cas_64((volatile uint64_t *)&(ht->table_hdr), (uint64_t)ohdr, (uint64_t)nhdr)){
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

static uint32_t assimilate(lfht_t *ht, lfht_hdr_t *from_hdr, uint64_t key, uint32_t hash,  uint32_t count);

static inline void _migrate_table(lfht_t *ht, lfht_hdr_t *hdr, uint64_t key, uint32_t hash){
  unsigned int table_state = atomic_load(&ht->state);

  if (table_state == EXPANDING){
    /* gotta pitch in and do some migrating */
    
    uint32_t moved = assimilate(ht, hdr->next, key, hash,  MIGRATIONS_PER_ACCESS);
    if (moved <  MIGRATIONS_PER_ACCESS){
      /* the move has finished! */
      atomic_store(&hdr->next->assimilated, true);
      atomic_store(&ht->state, EXPANDED);
    }
  }
}


static bool _lfht_add(lfht_t *ht, uint64_t key, uint64_t val, bool external){
  uint32_t hash, mask, j, i;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  bool retval;

  retval = false;
  
  
  assert( ! is_assimilated(key) );
  assert(val != TOMBSTONE);

  if (ht == NULL  || ht->table_hdr == NULL || key == 0  || val == TOMBSTONE){
    return retval;
  }
  
  hash = jenkins_hash_ptr((void *)key);

 retry:
  
  hdr = ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;
  

  /* only external calls pay the migration tax */
  if ( external ){ _migrate_table(ht, hdr, key, hash); }
  
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
	  _grow_table(ht);
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

  /* slow thread last gasp */
  if (atomic_load(&hdr->assimilated)){
    /* could have a fail count; might also not want to pay tax each time */
    goto retry;
  }

  
  return retval;
}


bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val){
  return _lfht_add(ht, key, val, true);
}


bool lfht_remove(lfht_t *ht, uint64_t key){
  uint32_t hash, mask, j, i;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  lfht_entry_t entry;
  bool retval;

  retval = false;
  
  if (ht == NULL || ht->table_hdr == NULL || key == 0){
    return retval;
  }

  hash = jenkins_hash_ptr((void *)key);

 retry:
  
  hdr = ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;

  _migrate_table(ht, hdr, key, hash);
  
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

  /* slow thread last gasp */
  if (atomic_load(&hdr->assimilated)){
    /* could have a fail count; might also not want to pay tax each time */
    goto retry;
  }


  return retval;
}


bool lfht_find(lfht_t *ht, uint64_t key, uint64_t *valp){
  uint32_t hash, mask, j, i;
  uint64_t kval;
  lfht_hdr_t *hdr;
  lfht_entry_t*  table;
  bool retval;

  retval = false;
  
    
  if (ht == NULL || ht->table_hdr == NULL || key == 0 || valp == NULL){
    return retval;
  }

  hash = jenkins_hash_ptr((void *)key);

 retry:
  
  hdr = ht->table_hdr;
  table = hdr->table;
  mask = hdr->max - 1;
  
  _migrate_table(ht, hdr, key, hash);
  
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

  /* slow thread last gasp */
  if (atomic_load(&hdr->assimilated)){
    /* could have a fail count; might also not want to pay tax each time */
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
      
      if ( ! is_assimilated(entry.key) ){
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
  
  return count;
}
