#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"

/* 
   From scratch implementation of Maged & Scott's 1996 podc paper, made clean
   by using a 128 CAS rather than a 64 CAS and a bit steal.

   https://www.research.ibm.com/people/m/michael/podc-1996.pdf

   https://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
*/

typedef struct pointer_s {
  volatile uintptr_t ptr;
  volatile uint64_t count;
} pointer_t;

typedef struct node_s {
  volatile pointer_t next;
} node_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
} lf_fifo_queue_t;


extern void lf_fifo_queue_init(lf_fifo_queue_t *queue);

extern void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

extern void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);

#endif

