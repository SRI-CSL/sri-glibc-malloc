#ifndef __QUEUE_H_
#define __QUEUE_H_

#include "atomic.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  volatile uintptr_t top;
  volatile uint64_t ocount;
} top_aba_t;

// Pseudostructure for lock-free list elements.
// The only requirement is that the 5th-8th byte of
// each element should be available to be used as
// the pointer for the implementation of a singly-linked
// list. 
struct queue_elem_t {
  //char 				*_dummy;   BD&DD
  volatile struct queue_elem_t 	*next;
};

typedef struct {
	uintptr_t 	_pad0[8];
	top_aba_t	both;
	uintptr_t 	_pad1[8];
} lf_fifo_queue_t;

#define LF_FIFO_QUEUE_STATIC_INIT	{{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}}
					  
/******************************************************************************/

static inline void lf_fifo_queue_init(lf_fifo_queue_t *queue);
static inline int lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);
static inline void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

/******************************************************************************/

static inline void lf_fifo_queue_init(lf_fifo_queue_t *queue)
{
	queue->both.top = 0;
	queue->both.ocount = 0;
}

/******************************************************************************/

static inline void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{
	top_aba_t head;
	top_aba_t next;

	while(1) {
		head.top = queue->both.top;
		head.ocount = queue->both.ocount;
		if (head.top == 0)
			return NULL;
		next.top = (uintptr_t)((struct queue_elem_t *)head.top)->next;
		next.ocount = head.ocount + 1;
		if (compare_and_swap64((unsigned long *)&(queue->both), *((uintptr_t*)&head), *((uintptr_t*)&next))) {
			return((void *)head.top);
		}
	}
}

/******************************************************************************/

static inline int lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element)
{
	top_aba_t old_top;
	top_aba_t new_top;
	
	while(1) {
		old_top.ocount = queue->both.ocount;
		old_top.top = queue->both.top;

		((struct queue_elem_t *)element)->next = (struct queue_elem_t *)old_top.top;
		new_top.top = (unsigned long)element;
		new_top.ocount = old_top.ocount + 1;
		if (compare_and_swap64((unsigned long *)&(queue->both), *((unsigned long *)&old_top), *((uintptr_t*)&new_top))) {
			return 0;
		}
	}
}

/******************************************************************************/

#endif
