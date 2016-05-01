#ifndef __LF_LIFO_QUEUE_H_
#define __LF_LIFO_QUEUE_H_

#include "atomic.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
	volatile unsigned long long top:48, count:16;
} top_aba_t;

/* Header for lock-free list elements. */
typedef struct queue_elem_t {
  volatile struct queue_elem_t 	*next;
} lf_queue_elem_t;


typedef struct {
	top_aba_t	both;
} lf_queue_t;


#define LF_QUEUE_STATIC_INIT  {{0, 0}}

static inline void lf_queue_init(lf_queue_t *queue)
{
	queue->both.top = 0;
	queue->both.count = 0;
}


static inline void *lf_dequeue(lf_queue_t *queue)
{
	top_aba_t head;
	top_aba_t next;

	while(1) {
		head.top = queue->both.top;
		head.count = queue->both.count;
		if (head.top == 0)
			return NULL;
		next.top = (uintptr_t)((struct queue_elem_t *)head.top)->next;
		next.count = head.count + 1;
		if (cas_64((volatile uint64_t *)&(queue->both),
			   *((uint64_t*)&head),
			   *((uint64_t*)&next))) {
			return ((void *)head.top);
		}
	}
}
static inline int lf_enqueue(lf_queue_t *queue, void *element)
{
	top_aba_t old_top;
	top_aba_t new_top;
	void *next;
	
	while(1) {
		old_top.count = queue->both.count;
		old_top.top = queue->both.top;
		next = (void *)old_top.top;
		((struct queue_elem_t *)element)->next = (struct queue_elem_t *)next;
		new_top.top = (unsigned long)element;
		new_top.count = old_top.count + 1;
		if (cas_64((volatile uint64_t *)&(queue->both),
			   *((uint64_t *)&old_top),
			   *((uint64_t *)&new_top))) {
			return 0;
		}
	}
}

#endif
