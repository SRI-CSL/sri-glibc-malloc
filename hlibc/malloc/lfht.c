#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "lfht.h"
#include "utils.h"
#include "sri_atomic.h"

#include <pthread.h>

static pthread_cond_t gate;

bool init_lfht(lfht_t *ht, uint32_t max){
  uint64_t sz;
  void *addr;

  pthread_cond_init(&gate, NULL);

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
    ht->table = NULL;
    if(retcode == 0){
      return true;
    }
  }
  return false;
}

bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;
    
  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;

    j = jenkins_hash_ptr((void *)key) & mask; 
    
    i = j;

    while (true) {

      entry = ht->table[i];
      
      if(entry.key == key){ break; } 

      if(entry.key == 0){
	if(cas_128((volatile lfht_entry_t *)&(ht->table[i]), *((lfht_entry_t *)&entry), *((lfht_entry_t *)&desired))){
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

bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;
  
  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = ht->table[i];

      if(entry.key == key){

	if(cas_128((volatile lfht_entry_t *)&ht->table[i], *((lfht_entry_t *)&entry), *((lfht_entry_t *)&desired))){
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

bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;

  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = ht->table[i];

      if(entry.key == key || entry.key == 0){
	if(cas_128((volatile lfht_entry_t *)&ht->table[i], *((lfht_entry_t *)&entry), *((lfht_entry_t *)&desired))){
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

bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp){
  uint32_t mask, j, i;
  uint64_t kval;
    
  mask = ht->max - 1;
  j = jenkins_hash_ptr((void *)key) & mask;
  i = j;
  
  if(ht != NULL && key != 0 && valp != NULL){

    while (true) {

      kval = read_64((volatile uint64_t *)&ht->table[i].key);

      if(kval == 0){
	return false;
      }

      if(kval == key){
	*valp = ht->table[i].val;
	return true;
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }
      
    }
  }

  return false;
}
  


void lfht_stats(FILE* fp, const char* name, lfht_t *ht, uintptr_t tombstone){
  uint32_t index, count = 0, tombstoned = 0;
  lfht_entry_t entry;
  for(index = 0; index < ht->max; index++){
    entry = ht->table[index];
    if(entry.key != 0){
      //fprintf(fp, "[%d]: %p => %"PRIuPTR"\n", index, (void*)entry.key, entry.val);
      if(entry.val != tombstone){  
	count++; 
      } else {
	tombstoned++;
      }
    }
  }
  fprintf(fp, 
	  "%s: max = %"PRIu32", count = %"PRIu32" tombstoned = %"PRIu32" total = %"PRIu32"\n",
	  name, ht->max, count, tombstoned, count + tombstoned);
  fflush(fp);
}
