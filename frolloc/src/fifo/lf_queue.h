#ifndef __LF_FIFO_QUEUE_H_
#define __LF_FIFO_QUEUE_H_

#include "atomic.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

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

static node_t end = {{0, 0}};

static inline void lf_fifo_queue_init(lf_fifo_queue_t *queue)
{
  assert(queue != NULL);
  
  queue->head.ptr = (uintptr_t)&end;
  queue->tail.ptr = (uintptr_t)&end;

}

static inline void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{

  return NULL;
}

static inline bool eq(pointer_t* lhs, volatile pointer_t* rhs){
  return lhs->ptr == rhs->ptr && lhs->count == rhs->count;
}

static inline void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element)
{
  node_t* node = (node_t*)element;

  
  pointer_t next;
  pointer_t tail;

  pointer_t temp;

  node->next.ptr = 0;  //is this needed?
  
  while(true){

    tail = queue->tail;                //read tail.ptr and tail.count together
    next = ((node_t*)tail.ptr)->next;  //read next.ptr and next.count together
    
    if ( eq(&tail, &(queue->tail)) ){  // are tail and next consistent ?
      // was tail pointing to the last node?
      if ( next.ptr == 0 ){
	// try to link node at the end of the linked list
	temp.ptr = (uintptr_t)node;
	temp.count = next.count + 1;
	if ( cas_128((volatile u128_t *)&(((node_t*)tail.ptr)->next),
		     *((u128_t *)&next),
		     *((u128_t *)&temp)) ){
	  break;
	} 
      } else {
	//tail was not pointing to the last node
	//try to swing tail to the next node
	temp.ptr = next.ptr;
	temp.count = tail.count + 1;
	cas_128((volatile u128_t *)&queue->tail, *((u128_t *)&tail), *((u128_t *)&temp));
      }
      
    }
  }
  // enqueue is done, Try to swing tail to the inserted node
  temp.ptr = (uintptr_t)node;
  temp.count = tail.count + 1;
  cas_128((volatile u128_t *)&queue->tail, *((u128_t *)&tail), *((u128_t *)&temp));
  

}


#endif

