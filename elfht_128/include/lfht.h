/* Lock Free Hash Table */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <pthread.h>


typedef struct lfht_entry_s {
  uintptr_t  key;
  uintptr_t  val;
} lfht_entry_t;


typedef struct lfht_s {
  atomic_bool expanding;
  atomic_int threads_inside;
  atomic_int threads_waiting;
  pthread_mutex_t lock;
  pthread_cond_t gate;
  
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  //the sizeof the table 
  uint64_t sz;
  //the number of items in the table
  atomic_uint_least32_t count;
  //the table
  lfht_entry_t *table;
} lfht_t;

/* 
  Initializes an uninitialized lfht_t struct.
  Used like so:

  lfht_t ht;
  bool success = init_lfht(&ht, 4096);
  if(success){
   //off to races
  
  }
 
   - max is the maximum number of key value pairs that the table will be able to store. 
     It is a hard limit.

*/
extern bool init_lfht(lfht_t *ht, uint32_t max);

extern bool delete_lfht(lfht_t *ht);

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  

#endif
