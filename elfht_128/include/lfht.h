/* Lock Free Hash Table */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <pthread.h>


#define RESIZE_RATIO 0.6

/* 
   Just a placeholder to mark where we *really* should be using
   lfht_delete if we had it. Note that in our world 0 is an 
   invalid value for either a descriptor ptr or the size of
   a mmapped region.
*/
#define TOMBSTONE 0

typedef struct lfht_entry_s {
  uintptr_t  key;
  uintptr_t  val;
} lfht_entry_t;


typedef struct lfht_s {

  // Version number of the hash table
  atomic_uint version;
  // flag to indicate we are in the process of growing the table
  atomic_bool expanding;
  // count of threads past the gate
  atomic_int threads_inside;
  // count of threads waiting at the gate
  atomic_int threads_waiting;
  // lock for the gate
  pthread_mutex_t gate_lock;
  // the gate
  pthread_cond_t gate;
  // lock for the grower
  pthread_mutex_t grow_lock;
  // the var the grower  waits on
  pthread_cond_t grow_var;
  // Need lock for election winner
  pthread_mutex_t nominee_lock;
  
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  // threshold beyond which we should grow the table
  uint32_t threshold;
  //the "sizeof" the mmapped region that is the table 
  uint64_t sz;
  //the number of non-zero keys in the table
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
  
extern void lfht_stats(FILE* fp, const char* name, lfht_t *ht);

#endif
