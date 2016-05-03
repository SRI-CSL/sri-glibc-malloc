#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"

#ifdef SRI_DEBUG
#include "../util.h"
#endif

typedef struct {
  uintptr_t ptr:48, count:16;
} pointer_t;


typedef struct queue_elem_t {
  volatile pointer_t next;
} lf_queue_elem_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
#ifdef SRI_DEBUG
  atomic_ulong length;
#endif
} lf_queue_t;

#define LF_QUEUE_STATIC_INIT  {{0, 0}, {0, 0}}

#define LF_ELEM_PTR(X) ((lf_queue_elem_t *)(intptr_t)X)

extern void lf_queue_init(lf_queue_t *queue);

extern void *lf_dequeue(lf_queue_t *queue);

extern void lf_enqueue(lf_queue_t *queue, void *element);

#endif

