#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


typedef struct {
  unsigned long long  ptr:48, count:16;
} pointer_t;


typedef struct queue_elem_t {
  volatile pointer_t next;
} lf_queue_elem_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
} lf_queue_t;

#define LF_QUEUE_STATIC_INIT  {0, 0}

extern void lf_queue_init(lf_queue_t *queue);

extern void *lf_dequeue(lf_queue_t *queue);

extern void lf_enqueue(lf_queue_t *queue, void *element);

#endif

