#include <stdlib.h>
#include <sys/mman.h>

#include "lfht.h"
#include "util.h"
#include "atomic.h"

static bool _should_grow_table(lfht_t *ht);

static bool _grow_table(lfht_t *ht);
  
/* N.B we will start with a simplistic version (i.e. no barriers etc) and work our way up to heaven */
static inline void _enter_(lfht_t *ht){
  const atomic_bool _expanding_ = atomic_load_explicit(&ht->expanding, memory_order_relaxed);

  if(_expanding_){

    /* we need to wait */
    pthread_mutex_lock(&ht->lock);

    atomic_fetch_add(&ht->threads_waiting, 1);

    while(atomic_load_explicit(&ht->expanding, memory_order_relaxed)){
	
      pthread_cond_wait(&ht->gate, &ht->lock); 

    }

    atomic_fetch_sub(&ht->threads_waiting, 1);

    atomic_fetch_add(&ht->threads_inside, 1);
 
  } else {

    /* we should see if the table needs to grow, and, we are the chosen one */

    if(_should_grow_table(ht)){

      _grow_table(ht);

    } else {

      atomic_fetch_add(&ht->threads_inside, 1);

    }
  }

}

static inline void _exit_(lfht_t *ht){
  
  atomic_fetch_sub(&ht->threads_inside, 1);
  
}


/*
  check to see if the table needs to grow, and if so see if we are the one.
  also a good place to make sure there are no laggards waiting at the gate.
 */
static bool _should_grow_table(lfht_t *ht){
  bool retval = false;

  /* grow the table */

  
  
  return retval;
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

  atomic_init(&ht->expanding, false);
  atomic_init(&ht->threads_inside, 0);
  atomic_init(&ht->threads_waiting, 0);
  atomic_init(&ht->count, 0);
    
  pthread_mutex_init(&ht->lock, NULL);
  pthread_cond_init(&ht->gate, NULL);
    
  const uint64_t sz = max * sizeof(lfht_entry_t);
  
  const void* addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  
  if (addr != MAP_FAILED) {
    ht->table = (lfht_entry_t *)addr;
    ht->max = max;
    ht->sz = sz;
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
  if(retcode == 0){
    retval = true;
  }

  return retval;

}

bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val){
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
    
    if(entry.key == 0){
      if(cas_128((volatile u128_t *)&(ht->table[i]), 
		 *((u128_t *)&entry), 
		 *((u128_t *)&desired))){
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

bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val){
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
    
    if(entry.key == key || entry.key == 0){
      if(cas_128((volatile u128_t *)&ht->table[i], 
		 *((u128_t *)&entry), 
		 *((u128_t *)&desired))){
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
  
