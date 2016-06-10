#ifdef SRI_MALLOC_LOG
#include "debug.h"

#include <atomic.h>

static volatile int tid_counter = 0;
static __thread  int tid = -1;


static inline void LOCK_ARENA(mstate av, int site){
  int self;
  if(tid == -1){
    tid = catomic_exchange_and_add (&tid_counter, 1);
  }
  self = tid;
  
  (void)mutex_lock(&(av->mutex));
  log_lock_event(LOCK_ACTION, av, av->arena_index, site, tid);

}


static inline void UNLOCK_ARENA(mstate av, int site){

  (void)mutex_unlock(&(av->mutex));
  log_lock_event(UNLOCK_ACTION, av, av->arena_index, site, tid);

}

#else

static inline void LOCK_ARENA(mstate av, int site){
  (void)mutex_lock(&(av->mutex));
}
static inline void UNLOCK_ARENA(mstate av, int site){
  (void)mutex_unlock(&(av->mutex));
}

#endif
