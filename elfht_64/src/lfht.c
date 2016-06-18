#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"


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
	assert(ht->state == INITIAL || ht->state == EXPANDED);
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


bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val){
  uint32_t mask, j, i;
  lfht_entry_t*  table;
  lfht_entry_t entry, desired;

  assert(val != TOMBSTONE);

  if(ht != NULL  && ht->table_hdr != NULL  && key != 0  && val != TOMBSTONE){
    table = ht->table_hdr->table;
    desired.key = key;
    desired.val = val;
    mask = ht->table_hdr->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = table[i];

      if(entry.key == 0){
	if(cas_64((volatile uint64_t *)&(table[i].key), entry.key, desired.key)){
	  //iam: discuss
	  table[i].val = val;

	  const uint_least32_t count = atomic_fetch_add(&ht->table_hdr->count, 1);

	  if(count + 1 > ht->table_hdr->threshold){
	    _grow_table(ht);
	  }
	  
	  
	  return true;
	} else {
	  continue;
	}
      }
      
      if(entry.key == key){
	if(cas_64((volatile uint64_t *)&(table[i].val), entry.val, desired.val)){
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

bool lfht_remove(lfht_t *ht, uint64_t key){
  uint32_t mask, j, i;
  lfht_entry_t*  table;
  lfht_entry_t entry, desired;
  
  if(ht != NULL  && ht->table_hdr != NULL  && key != 0){
    table = ht->table_hdr->table;
    mask = ht->table_hdr->max - 1;
    desired.key = key;
    desired.val = TOMBSTONE;

    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = table[i];

      if(entry.key == key){
	if(cas_64((volatile uint64_t *)&(table[i].val), entry.val, desired.val)){
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
  uint32_t mask, j, i;
  uint64_t kval;
  lfht_entry_t*  table;
    
  if(ht != NULL  && ht->table_hdr != NULL  && key != 0 && valp != NULL){
    table = ht->table_hdr->table;
    mask = ht->table_hdr->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
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
  



uint32_t assimilate(lfht_tbl_hdr_t *to_hdr, lfht_tbl_hdr_t *from_hdr, uint32_t hash,  uint32_t count){


  return 0;
}
