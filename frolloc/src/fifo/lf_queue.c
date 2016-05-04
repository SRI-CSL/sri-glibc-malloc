#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "atomic.h"
#include "lf_queue.h"

#ifdef SRI_DEBUG
#include <stdatomic.h>
#endif

/* 
   From scratch implementation of Maged & Scott's 1996 podc paper.

   https://www.research.ibm.com/people/m/michael/podc-1996.pdf

   https://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
*/

static bool sane_queue(lf_queue_t *queue){
  uint32_t length = queue->length;
  pointer_t index = queue->head;
  uint32_t i;

  if (LF_ELEM_PTR(index.ptr) != &queue->sentinel) {
    return false;
  }

  for(i = 0; i < length; i++){
    if(index.ptr == 0){ break; }
    index =  LF_ELEM_PTR(index.ptr)->next;
  }
  
  return (index.ptr == 0 &&  i == length);
}


void lf_queue_init(lf_queue_t *queue)
{
  assert(queue != NULL);
  queue->sentinel.next.ptr = 0;
  queue->sentinel.next.count = 0;
  queue->head.ptr = (uintptr_t)&queue->sentinel;
  queue->tail.ptr = (uintptr_t)&queue->sentinel;
#ifdef SRI_DEBUG
  atomic_init(&queue->length, 1);
#endif
  
  assert(sane_queue(queue));

}

static inline bool eq(volatile pointer_t lhs, volatile pointer_t rhs){
  return lhs.ptr == rhs.ptr && lhs.count == rhs.count;
}


void *lf_dequeue(lf_queue_t *queue)
{
  pointer_t head, tail, next, temp;
  lf_queue_elem_t* retval;

  assert(sane_queue(queue));

  assert(queue != NULL);

  retval = NULL;

  while(true){

    head = queue->head;       //read the head
    tail = queue->tail;       //read the tail

    next = LF_ELEM_PTR(head.ptr)->next;    //read the head.ptr->next

    if( eq(head, queue->head) ){ //are head, tail, and next consistent

      if( head.ptr == tail.ptr ){   //is queue empty or tail falling behind

	if(next.ptr == 0){
	  assert(sane_queue(queue));
	  return NULL;
	}
	//tail is falling behind. Try to advance it.
	temp.ptr = next.ptr;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail,*((uint64_t *)&tail),*((uint64_t *)&temp));
	
      } else { //no need to deal with tail.

	//read the value before the cas
	//otherwise, another dequeue might free the next node
	retval = LF_ELEM_PTR(next.ptr);
	//try to swing head to the next node
	temp.ptr = next.ptr;
	temp.count = head.count + 1;

        /*
	 * SOMETHING IS WRONG HERE:
	 * Either the invariant we assume is not a real invariant or this CAS should update head->next
	 * instead of head.
	 */
	if( cas_64((volatile uint64_t *)&queue->head,*((uint64_t *)&head),*((uint64_t *)&temp)) ){
	  break; //dequeue is done, exit loop;
	}
	retval = NULL;
      }
    }
  }

  atomic_decrement(&queue->length);

  assert(sane_queue(queue));

  return retval;
}

void lf_enqueue(lf_queue_t *queue, void *element)
{
  lf_queue_elem_t* node = (lf_queue_elem_t*)element;
  pointer_t next, tail, temp;
 
  assert(sane_queue(queue));

  assert(queue != NULL);
  assert(node != NULL);

  if (element == (void*) 0x7ffff7ecd480) {
    fprintf(stderr, "HERE\n");    
  }

  node->next.ptr = 0;  //is this needed?
  
  while(true){
    tail = queue->tail;                  //read tail.ptr and tail.count together
    next = LF_ELEM_PTR(tail.ptr)->next;  //read next.ptr and next.count together
    
    if ( eq(tail, queue->tail) ){  // are tail and next consistent ?
      // was tail pointing to the last node?
      if ( next.ptr == 0 ){
	// try to link node at the end of the linked list
	temp.ptr = (uintptr_t)node;
	temp.count = next.count + 1;
	if ( cas_64((volatile uint64_t *)&(LF_ELEM_PTR(tail.ptr)->next),*((uint64_t *)&next),*((uint64_t *)&temp)) ){
	  break;
	} 
      } else {
	//tail was not pointing to the last node
	//try to swing tail to the next node
	temp.ptr = next.ptr;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail,*((uint64_t *)&tail),*((uint64_t *)&temp));
      }
    }
  }
  // enqueue is done, Try to swing tail to the inserted node
  temp.ptr = (uintptr_t)node;
  temp.count = tail.count + 1;
  cas_64((volatile uint64_t *)&queue->tail,*((uint64_t *)&tail),*((uint64_t *)&temp));

#ifdef SRI_DEBUG
  atomic_increment(&queue->length);
#endif

  assert(sane_queue(queue));

}


