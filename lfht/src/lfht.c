#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"


bool init_lfht(lfht_t *ht, uint32_t max){
  uint64_t sz;
  void *addr;
  if(ht != NULL && max != 0){
    sz = max * sizeof(lfht_entry_t);
    addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (addr != MAP_FAILED) {
      ht->table = (lfht_entry_t *)addr;
      ht->max = max;
      ht->sz = sz;
      return true;
    }
  }
  return false;
}

bool delete_lfht(lfht_t *ht){
  int retcode;
  if(ht != NULL && ht->table != NULL){
    retcode = munmap(ht->table, ht->sz);
    if(retcode == 0){
      return true;
    }
  }
  return false;
}

bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j;
  lfht_entry_t *entry, desired;
  
  if(ht != NULL  && key != 0){

    desired.key = key;
    desired.val = val;
    
    mask = ht->max - 1;

    j = jenkins_hash_ptr((void *)key);

    while(1){
      
    restart:

      entry = ht->table + j;

      if(entry->key == 0){
	if(cas_128((volatile u128_t *)entry, *((u128_t *)entry), *((u128_t *)&desired))){
	  return true;
	} else {
	  goto restart;
	}
      }
      
      
      j++;
      j &= mask;


    }

  }
  
  return false;
}

bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp){


  return false;
}
  
