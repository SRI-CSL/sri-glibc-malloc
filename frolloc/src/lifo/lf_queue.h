#ifndef __LF_LIFO_QUEUE_H_
#define __LF_LIFO_QUEUE_H_

#include "atomic.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  volatile uint64_t ptr:48, aba:16;
} aba_ptr_t;

/* Header for lock-free list elements. */
typedef struct queue_elem_t {
  volatile aba_ptr_t next;
} lf_queue_elem_t;


typedef struct {
  aba_ptr_t	both;
} lf_queue_t;

#define LF_ELEM_PTR(X) ((lf_queue_elem_t *)(intptr_t)X)

#define LF_QUEUE_STATIC_INIT  {{0, 0}}

extern void lf_queue_init(lf_queue_t *queue);

extern void *lf_dequeue(lf_queue_t *queue);

extern int lf_enqueue(lf_queue_t *queue, void *element);

#endif
