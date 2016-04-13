/* Lock Free Hash Table */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>


typedef struct lfht_entry_s {
  uintptr_t  key;
  uintptr_t  val;
} lfht_entry_t;


typedef struct lfht_s {
  uint64_t max;
  lfht_entry_t *table;
} lfht_t;


extern bool init_lfht(lfht_t *ht, uint64_t max);

extern bool delete_lfht(lfht_t *ht, uint64_t max);

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  

#endif
