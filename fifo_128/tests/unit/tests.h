#include "lf_fifo_queue.h"

typedef struct elem_s {
  volatile node_t node;
  uint64_t number;
  char pad[8];
} elem_t;


elem_t* elem_alloc(){ return malloc(sizeof(elem_t)); }
