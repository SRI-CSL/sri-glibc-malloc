#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <inttypes.h>

#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"

static bool _grow_table(lfht_t *ht);
  
/* 
 * N.B we will start with a simplistic version (i.e. no barriers etc) and work our way up to heaven   
 */
static void _enter_(lfht_t *ht){

  // think about memory order
  const atomic_uint _version_ = atomic_load_explicit(&ht->version, memory_order_relaxed);
  const atomic_bool _expanding_ = atomic_load_explicit(&ht->expanding, memory_order_relaxed);
  
  if(_expanding_){

    /* we need to wait */
    pthread_mutex_lock(&ht->gate_lock);

    /* register ourselves as a waiting thread */
    atomic_fetch_add(&ht->threads_waiting, 1);

    /* wait until the table is no longer waiting */
    while(atomic_load_explicit(&ht->expanding, memory_order_relaxed)){
      
      pthread_cond_wait(&ht->gate, &ht->gate_lock); 
      
    }

    /* remove ourselves from the waiting thread tally */
    atomic_fetch_sub(&ht->threads_waiting, 1);

    /* release the lock */
    pthread_mutex_unlock(&ht->gate_lock);
 
  } else if(ht->count >= ht->threshold){

    /* not expanding; but need to expand */

    /* first thread to get this lock gets to do the dirty work */
    pthread_mutex_lock(&ht->nominee_lock);

    /* has the table already been grown */
    if(atomic_load(&ht->version) == _version_) {
      
      atomic_store(&ht->expanding, true);
    
      pthread_mutex_lock(&ht->grow_lock);

     while(atomic_load_explicit(&ht->threads_inside, memory_order_relaxed) > 0){
      
	pthread_cond_wait(&ht->grow_var, &ht->grow_lock); 
	
      }
      
      _grow_table(ht);

      atomic_fetch_add(&ht->version, 1);

      pthread_mutex_unlock(&ht->grow_lock);

      atomic_store(&ht->expanding, false);

      pthread_mutex_lock(&ht->gate_lock);

      pthread_cond_broadcast(&ht->gate);

      pthread_mutex_unlock(&ht->gate_lock);
    }
    
    pthread_mutex_unlock(&ht->nominee_lock);
  }
  
  
  atomic_fetch_add(&ht->threads_inside, 1);
  
}


static void _exit_(lfht_t *ht){

  // we remove ourselves from the insider tally
  const atomic_int _inside_ = atomic_fetch_sub(&ht->threads_inside, 1);

  // if we are expanding we need to let the grower know if we are the last one through.
  const atomic_bool _expanding_ = atomic_load_explicit(&ht->expanding, memory_order_relaxed);

  /* last one through lets the grower know */
  if( _expanding_  && _inside_ == 1){

    pthread_mutex_lock(&ht->grow_lock);

    pthread_cond_signal(&ht->grow_var);

    pthread_mutex_unlock(&ht->grow_lock);
  
  }

  // good place to make some general state invariant assertions 
  assert(ht->count <= ht->max);
  
}



/*
 * Insert key, val into a freshly allocated table
 * - n = size of this new table.
 */
static void clean_insert(lfht_entry_t *table, uint32_t n, uintptr_t key, uintptr_t val) {
  uint32_t j, mask;

  assert(table != NULL && key != 0 && val != TOMBSTONE);

  mask = n - 1;
  j = jenkins_hash_ptr((void *)key) & mask; 
  while (table[j].key != 0) {
    j ++;
    j &= mask;
  }

  table[j].key = key;
  table[j].val = val;
}

/*
 * Cleanup + make a copy of the current table into a new mmapped region
 */
static bool _grow_table(lfht_t *ht){
  uint32_t i, n, new_n, new_count;
  lfht_entry_t *new_table;
  uint64_t new_sz;

  n = ht->max;
  if (n >= UINT32_MAX/2) {
    // OVERFLOW. We can't grow the table.
    return false;
  }

  // TODO: in some cases, we could just keep the same size (just remove the tombstones)?
  new_n = n << 1;
  assert(is_power_of_two(new_n));
  new_sz  = new_n * sizeof(lfht_entry_t);
  new_table = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (new_table == MAP_FAILED) {
    return false;
  }

  new_count = 0;
  for (i=0; i<n; i++) {
    if (ht->table[i].key != 0 && ht->table[i].val != TOMBSTONE) {
      assert(new_count < new_n); // Otherwise clean_insert will loop
      clean_insert(new_table, new_n, ht->table[i].key, ht->table[i].val);
      new_count ++;
    }
  }

  munmap(ht->table, ht->sz); // free the old version

  ht->max = new_n;
  ht->threshold = (uint32_t)(new_n * RESIZE_RATIO);
  ht->sz = new_sz;
  atomic_store(&ht->count, new_count);
  ht->table = new_table;

  return true;
}




bool init_lfht(lfht_t *ht, uint32_t max){
  bool retval = false;

  if(ht == NULL || max == 0){ 
    return retval; 
  }

  assert(is_power_of_two(max));

  const uint64_t sz = max * sizeof(lfht_entry_t);
  
  atomic_init(&ht->version, 0);
  atomic_init(&ht->expanding, false);
  atomic_init(&ht->threads_inside, 0);
  atomic_init(&ht->threads_waiting, 0);
  pthread_mutex_init(&ht->gate_lock, NULL);
  pthread_cond_init(&ht->gate, NULL);
  pthread_mutex_init(&ht->grow_lock, NULL);
  pthread_cond_init(&ht->grow_var, NULL);
  pthread_mutex_init(&ht->nominee_lock, NULL);
  ht->max = max;
  ht->threshold = (uint32_t)(max * RESIZE_RATIO);
  ht->sz = sz;
  atomic_init(&ht->count, 0);
  ht->table = NULL;
  
  
  const void* addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  
  if (addr != MAP_FAILED) {
    ht->table = (lfht_entry_t *)addr;
    retval = true;
  }

  return retval;
}

bool delete_lfht(lfht_t *ht){
  int retval = false;
  
  if(ht == NULL || ht->table == NULL){ 
    return retval; 
  }

  const int retcode = munmap(ht->table, ht->sz);
    
  ht->table = NULL;

  pthread_mutex_destroy(&ht->gate_lock);
  pthread_cond_destroy(&ht->gate);
  pthread_mutex_destroy(&ht->grow_lock);
  pthread_cond_destroy(&ht->grow_var);
  pthread_mutex_destroy(&ht->nominee_lock);

  
  if(retcode == 0){
    retval = true;
  }

  return retval;

}

bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t j, i;
  lfht_entry_t entry, desired;

  bool retval = false;

  assert(val != TOMBSTONE);
  
  if(ht == NULL || key == 0){
    return retval;
  }

  desired.key = key;
  desired.val = val;
  
  _enter_(ht);

  const uint32_t mask = ht->max - 1;
  
  j = jenkins_hash_ptr((void *)key) & mask; 
  
  i = j;


  while (true) {
    
    entry = ht->table[i];

    if(entry.key == key){ break; } 
    
    if(entry.key == 0){
      if(cas_128((volatile u128_t *)&(ht->table[i]), 
		 *((u128_t *)&entry), 
		 *((u128_t *)&desired))){
	
	atomic_fetch_add(&ht->count, 1);
	
	retval = true;
	break;
      } else {
	continue;
      }
    }
    
    i++;
    i &= mask;
    
    if( i == j ){ break; }
    
  }
  
  _exit_(ht);
  
  return retval;
}

bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t j, i;
  lfht_entry_t entry, desired;
  bool retval = false;

  if(ht == NULL || key == 0){
    return retval;
  }

  desired.key = key;
  desired.val = val;

  _enter_(ht);

  const uint32_t mask = ht->max - 1;

  j = jenkins_hash_ptr((void *)key) & mask;
  i = j;

  while (true) {
    
    entry = ht->table[i];
    
    if(entry.key == key){
      
      if(cas_128((volatile u128_t *)&ht->table[i], 
		 *((u128_t *)&entry), 
		 *((u128_t *)&desired))){
	retval = true;
	break;
	
      } else {
	continue;
      }
    } else if(entry.key == 0){
      break; 
    }
    
    i++;
    i &= mask;
    
    if( i == j ){ break; }
    
  }
    
  _exit_(ht);

  return retval;
}

/* to TOMBTONE a key you MUST use lfht_update  */
bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t j, i;
  lfht_entry_t entry, desired;
  bool retval = false;

  assert( val != TOMBSTONE );
  
  if(ht == NULL || key == 0){
    return retval;
  }

  desired.key = key;
  desired.val = val;
  
  _enter_(ht);

  const uint32_t mask = ht->max - 1;
  
  j = jenkins_hash_ptr((void *)key) & mask;
  i = j;
  
  while (true) {
    
    entry = ht->table[i];
    
    if(entry.key == key || entry.key == 0){

      if(cas_128((volatile u128_t *)&ht->table[i], 
		 *((u128_t *)&entry), 
		 *((u128_t *)&desired))){


	if( ! entry.key ){

	  atomic_fetch_add(&ht->count, 1);

	}
	
	retval = true;
	break;
      } else {
	continue;
      }
    }
    
    i++;
    i &= mask;
    
    if( i == j ){ break; }
    
  }

  _exit_(ht);

  return retval;
}

bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp){
  uint32_t j, i;
  uint64_t kval;
  bool retval = false;

  if(ht == NULL || key == 0 || valp == NULL){
    return retval;
  }

  _enter_(ht);
   
  const uint32_t mask = ht->max - 1;

  j = jenkins_hash_ptr((void *)key) & mask;
  i = j;
  

  while (true) {
    
    kval = read_64((volatile uint64_t *)&ht->table[i].key);
    
    if(kval == 0){
      break;
    }
    
    if(kval == key){
      *valp = ht->table[i].val;
      retval = true;
      break;
    }
    
    i++;
    i &= mask;
    
    if( i == j ){ break; }
    
  }
  
  _exit_(ht);
  
  return retval;
}
  
void lfht_stats(FILE* fp, const char* name, lfht_t *ht){
  fprintf(fp, "%s: version = %d, max = %"PRIu32", count = %"PRIu32"\n", name, ht->version, ht->max, ht->count);
  fflush(fp);
}
