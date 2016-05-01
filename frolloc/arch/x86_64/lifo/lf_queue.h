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

extern void lf_queue_init(lf_queue_t *queue);

extern void *lf_dequeue(lf_queue_t *queue);

extern int lf_enqueue(lf_queue_t *queue, void *element);

#endif
