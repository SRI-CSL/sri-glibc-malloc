#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


typedef struct {
  unsigned long long  ptr:48, count:16;
} pointer_t;


typedef struct node_s {
  volatile pointer_t next;
} node_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
} lf_queue_t;



extern void lf_queue_init(lf_queue_t *queue);

extern void *lf_dequeue(lf_queue_t *queue);

extern void lf_enqueue(lf_queue_t *queue, void *element);

#endif

