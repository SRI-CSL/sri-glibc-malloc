#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


typedef struct {
  unsigned long long  top:48, count:16;
} pointer_t;

typedef union {
  pointer_t aba;
  uintptr_t pointer;
} aba_pointer_t;

static inline uintptr_t  aba_pointer_2_uintptr(aba_pointer_t abaptr){
  uintptr_t pointer = abaptr.pointer;
  pointer >>= 16;
  return pointer;
}


typedef struct lf_queue_elem_s {
  volatile pointer_t next;
} lf_queue_elem_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
} lf_fifo_queue_t;



extern void lf_fifo_queue_init(lf_fifo_queue_t *queue);

extern void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

extern void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);

#endif

