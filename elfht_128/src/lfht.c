#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"

static bool _grow_table(lfht_t *ht);
  
/* N.B we will start with a simplistic version (i.e. no barriers etc) and work our way up to heaven

   Questions:
   
   - When do we grow?

   - Does delete decrement the table count?

   - Should threads that don't win the grow lottery continue or wait?

   - While the grower waits for the inside count to hit zero who signals it? Assuming
   it waits on a condvar...

 */

static void _enter_(lfht_t *ht){

  const atomic_bool _expanding_ = atomic_load_explicit(&ht->expanding, memory_order_relaxed);

  if(_expanding_){

    /* we need to wait */

    pthread_mutex_lock(&ht->gate_lock);
    
    atomic_fetch_add(&ht->threads_waiting, 1);
    
    while(atomic_load_explicit(&ht->expanding, memory_order_relaxed)){
      
      pthread_cond_wait(&ht->gate, &ht->gate_lock); 
      
    }
    
    atomic_fetch_sub(&ht->threads_waiting, 1);
        
    pthread_mutex_unlock(&ht->gate_lock);
 
  } else if(ht->count >= ht->threshold){


    /* first thread through gets to do the dirty work */
    pthread_mutex_lock(&ht->grow_lock);

    /* has the table already been grown */
    if(ht->count >= ht->threshold){
      
      atomic_store(&ht->expanding, true);
    
      while(atomic_load_explicit(&ht->threads_inside, memory_order_relaxed) > 0){
      
	pthread_cond_wait(&ht->grow_var, &ht->grow_lock); 
	
      }
      
      _grow_table(ht);

      //still need to flick off the expanding and broadcast the waiters...
    }
    
    pthread_mutex_unlock(&ht->grow_lock);
    
  }
  
  
  atomic_fetch_add(&ht->threads_inside, 1);
  
}


static void _exit_(lfht_t *ht){

  // could look at the _expanding_ flag again, and signal the grower if we are expanding.
  const atomic_int _inside_ = atomic_fetch_sub(&ht->threads_inside, 1);

  const atomic_bool _expanding_ = atomic_load_explicit(&ht->expanding, memory_order_relaxed);

  /* last one through lets the grower know */
  if( _expanding_  && _inside_ == 0){

    pthread_mutex_lock(&ht->grow_lock);

    pthread_cond_signal(&ht->grow_var);

    pthread_mutex_unlock(&ht->grow_lock);
  
    
  }
  
  
}




static bool _grow_table(lfht_t *ht){
  bool retval = false;

  /* grow the table */

  
  
  return retval;
}




bool init_lfht(lfht_t *ht, uint32_t max){
  bool retval = false;

  if(ht == NULL || max == 0){ 
    return retval; 
  }

  const uint64_t sz = max * sizeof(lfht_entry_t);
  
  atomic_init(&ht->expanding, false);
  atomic_init(&ht->threads_inside, 0);
  atomic_init(&ht->threads_waiting, 0);
  pthread_mutex_init(&ht->gate_lock, NULL);
  pthread_cond_init(&ht->gate, NULL);
  pthread_mutex_init(&ht->grow_lock, NULL);
  pthread_cond_init(&ht->grow_var, NULL);
  ht->max = max;
  ht->threshold = (uint32_t)(max * RESIZE_RATIO);
  ht->sz = sz;
  atomic_init(&ht->count, 0);
  atomic_init(&ht->tombstoned, 0);
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

	if( val == TOMBSTONE && entry.val != TOMBSTONE ){

	  atomic_fetch_add(&ht->tombstoned, 1);

	}
	
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

	  if( entry.val == TOMBSTONE ){

	    atomic_fetch_sub(&ht->tombstoned, 1);

	  }

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
  
