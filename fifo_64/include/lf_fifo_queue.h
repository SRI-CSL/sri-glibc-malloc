#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


typedef struct {
  uint64_t  ptr:48, aba:16;
} aba_ptr_t;


typedef struct lf_queue_elem_s {
  volatile aba_ptr_t next;
} lf_queue_elem_t;


typedef struct {
  volatile aba_ptr_t head;
  volatile aba_ptr_t tail;
} lf_fifo_queue_t;

#define LF_ELEM_PTR(X) ((lf_queue_elem_t *)(intptr_t)X)

extern void lf_fifo_queue_init(lf_fifo_queue_t *queue);

extern void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

extern void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);

#endif

