/* Expanding Lock Free Hash Table (using only 64 bit CAS) */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

enum tombstone { TOMBSTONE = 0 };

#define RESIZE_RATIO 0.6


//(1 << 31) or 2^31
#define MAX_TABLE_SIZE ((uint32_t)0x80000000u)

#define KEY_ALIGNMENT  0x10

enum lfht_state { INITIAL, EXPANDING, EXPANDED };

typedef struct lfht_entry_s {
  volatile uint64_t  key;
  volatile uint64_t  val;
} lfht_entry_t;


typedef struct lfht_hdr_s {
  // flag to indicate if this table no longer contains relevant key/value pairs
  volatile atomic_bool assimilated;
  //the "sizeof" the mmapped region that is the header + table 
  uint64_t sz;
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  // threshold beyond which we should grow the table
  uint32_t threshold;
  //the number of non-zero keys in the table
  volatile atomic_uint_least32_t count;
  //pointer to the immediate predecessor table
  struct lfht_hdr_s *next;
  //the actual table
  lfht_entry_t *table;
} lfht_hdr_t;



typedef struct lfht_s {
  //the lfht_state of the table
  volatile atomic_uint state;
  volatile lfht_hdr_t *table_hdr;
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

/*
 * Insert or update the value of key in the table to val. 
 * 
 * - key must be 16 byte aligned.
 * - val *must* not be TOMBSTONE.
 *
 */
extern bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val);

/*
 * Remove the value of key in the table, i.e. set it to TOMBSTONE. 
 */
extern bool lfht_remove(lfht_t *ht, uint64_t key);


extern bool lfht_find(lfht_t *ht, uint64_t key, uint64_t *valp);
  

extern void lfht_dump(FILE* fp, lfht_t *ht);


#endif
