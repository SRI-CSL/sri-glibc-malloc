#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


/*
 *
 * If we insist that pointer_t must be 16 byte aligned we can 
 * steal 4 bits from the pointer to store an ABA tag. Not a very 
 * convincing ABA tag, but ... if the size of a decriptor is
 * a bigger power of two we can beef up the size of the ABA tag.
 *
 */
#define TAG_MASK    0xF

typedef struct {
  unsigned long long  ptr:60, count:4;
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

