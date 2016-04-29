#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"


/*
 * If we insist that pointer_t must be 16 byte aligned we can 
 * steal 4 bits from the pointer to store an ABA tag
 *
 */
#define TAG_MASK    0xF


typedef struct {
  uintptr_t 	ptr:60, count:4;
} pointer_t;


typedef struct node_s {
  volatile pointer_t next;
} node_t;


typedef struct {
  volatile pointer_t head;
  volatile pointer_t tail;
} lf_fifo_queue_t;


static inline void* get_ptr(pointer_t ptr){
  return ptr & ~TAG_MASK;
}

static inline uint8_t get_tag(pointer_t ptr){
  return (uint8_t)(ptr & TAG_MASK);
}

static inline make_pointer(void *ptr, uint8_t tag){
  assert((ptr & TAG_MASK) == 0);
  assert((tag & ~TAG_MASK) == 0);
  return ptr | tag;
}


static inline uint8_t inc_tag(uint8_t tag){
  return (tag + 1) & TAG_MASK;
}


                              


extern void lf_fifo_queue_init(lf_fifo_queue_t *queue);

extern void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

extern void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);

#endif

