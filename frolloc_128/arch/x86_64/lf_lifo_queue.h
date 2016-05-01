#ifndef __LF_LIFO_QUEUE_H_
#define __LF_LIFO_QUEUE_H_

#include "atomic.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  volatile uintptr_t top;
  volatile uint64_t ocount;
} top_aba_t;


/* Header for lock-free list elements. */
typedef struct queue_elem_t {
  volatile struct queue_elem_t 	*next;
} lf_queue_elem_t;


typedef struct {
  unsigned long long _pad0[8];
  top_aba_t	both;
  unsigned long long _pad1[8];
} lf_lifo_queue_t;


#define LF_LIFO_QUEUE_STATIC_INIT	{{0, 0}}

static inline void lf_lifo_queue_init(lf_lifo_queue_t *queue)
{
	queue->both.top = 0;
	queue->both.ocount = 0;
}


static inline void *lf_lifo_dequeue(lf_lifo_queue_t *queue)
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
		if (cas_128((volatile u128_t *)&(queue->both),
			   *((u128_t*)&head),
			   *((u128_t*)&next))) {
			return((void *)head.top);
		}
	}
}
static inline int lf_lifo_enqueue(lf_lifo_queue_t *queue, void *element)
{
	top_aba_t old_top;
	top_aba_t new_top;
	
	while(1) {
		old_top.ocount = queue->both.ocount;
		old_top.top = queue->both.top;

		((struct queue_elem_t *)element)->next = (struct queue_elem_t *)old_top.top;
		new_top.top = (unsigned long)element;
		new_top.ocount = old_top.ocount + 1;
		if (cas_128((volatile u128_t *)&(queue->both),
			   *((u128_t *)&old_top),
			   *((u128_t *)&new_top))) {
			return 0;
		}
	}
}

#endif
