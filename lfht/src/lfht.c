#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"


bool init_lfht(lfht_t *ht, uint64_t max){
  uint64_t sz;
  void *addr;
  if(ht != NULL && max != 0){
    sz = max * sizeof(lfht_entry_t);
    if(max < sz){
      addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
      if (addr != MAP_FAILED) {
	ht->table = (lfht_entry_t *)addr;
	ht->max = max;
	return true;
      }
    }
  }
  return false;
}

extern bool delete_lfht(lfht_t *ht, uint64_t max);

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  
