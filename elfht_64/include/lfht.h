/* Expanding Lock Free Hash Table (using only 64 bit CAS) */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>

enum lfht_state_t { INITIAL, EXPANDING, EXPANDED };


typedef struct lfht_entry_s {
  uintptr_t  key;
  uintptr_t  val;
} lfht_entry_t;


typedef struct lfht_tbl_hdr_s {
  // flag to indicate if this table contains relevant key/value pairs
  atomic_bool assimilated;
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  // threshold beyond which we should grow the table
  uint32_t threshold;
  //the "sizeof" the mmapped region that is the header + table 
  uint64_t sz;
  //the number of non-zero keys in the table
  atomic_uint_least32_t count;
  //pointer to the immediate predecessor table
  struct lfht_tbl_hdr_s* next;
  //the actual table
  lfht_entry_t*  table;
} lfht_tbl_hdr_t;



typedef struct lfht_s {
  //the lfht_state_t of the table
  atomic_uint state;
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  //the sizeof the table 
  uint64_t sz;
  lfht_entry_t *table;
} lfht_t;

/* 
 *  Initializes an uninitialized lfht_t struct.
 *  Used like so:
 *
 *  lfht_t ht;
 *  bool success = init_lfht(&ht, 4096);
 *  if(success){
 *   //off to races
 *  
 *  }
 * 
 *   - max is the initial number of key value pairs that the table will be able to store. 
 *
 */

extern bool init_lfht(lfht_t *ht, uint32_t max);

extern bool delete_lfht(lfht_t *ht);

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  

#endif
