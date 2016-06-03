#if SRI_MALLOC_LOG
#include "debug.h"

static inline void LOCK_ARENA(mstate av, int site){
  (void)mutex_lock(&(av->mutex));
  log_lock_event(LOCK_ACTION, av, av->arena_index, site);
}


static inline void UNLOCK_ARENA(mstate av, int site){
  (void)mutex_unlock(&(av->mutex));
  log_lock_event(UNLOCK_ACTION, av, av->arena_index, site);
}

#else

#define LOCK_ARENA(AV, S)   ((void)mutex_lock(&(AV->mutex)))

#define UNLOCK_ARENA(AV, S) ((void)mutex_unlock(&(AV->mutex)))

#endif
