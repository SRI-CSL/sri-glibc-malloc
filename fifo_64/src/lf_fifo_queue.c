#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "atomic.h"
#include "lf_fifo_queue.h"


/* 
   From scratch implementation of Maged & Scott's 1996 podc paper.

   https://www.research.ibm.com/people/m/michael/podc-1996.pdf

   https://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
*/

static lf_queue_elem_t end  __attribute__ ((aligned (16)));

void lf_fifo_queue_init(lf_fifo_queue_t *queue)
{
  assert(queue != NULL);
  end.next.ptr = 0;
  end.next.aba = 0;
  queue->head.ptr = (uintptr_t)&end;
  queue->tail.ptr = (uintptr_t)&end;

}

static inline bool eq(aba_ptr_t lhs, volatile aba_ptr_t rhs){
  return lhs.ptr == rhs.ptr && lhs.aba == rhs.aba;
  //return *((uint64_t *)lhs) == *((uint64_t *)rhs);
}


void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{
  aba_ptr_t head;
  aba_ptr_t tail;
  aba_ptr_t next;

  aba_ptr_t temp;

  lf_queue_elem_t* retval;
  
  assert(queue != NULL);

  retval = NULL;
  
  while(true){

    head = queue->head;       //read the head
    tail = queue->tail;       //read the tail
    next = LF_ELEM_PTR(head.ptr)->next;    //read the head.ptr->next

    if( eq(head, queue->head) ){ //are head, tail, and next consistent

      if( head.ptr == tail.ptr ){   //is queue empty or tail falling behind

	if(next.ptr == 0){
	  return NULL;
	}
	//tail is falling behind. Try to advance it.
	temp.ptr = next.ptr;
	temp.aba = tail.aba + 1;
	cas_64((volatile uint64_t *)&queue->tail,
		*((uint64_t *)&tail),
		*((uint64_t *)&temp));
	
      } else { //no need to deal with tail.

	//read the value before the cas
	//otherwise, another dequeue might free the next node
	retval = LF_ELEM_PTR(next.ptr);


	//try to swing head to the next node
	temp.ptr = next.ptr;
	temp.aba = head.aba + 1;

	if( cas_64((volatile uint64_t *)&queue->head,
		    *((uint64_t *)&head),
		    *((uint64_t *)&temp)) ){
	  break; //dequeue is done, exit loop;
	}
	retval = NULL;
      }
    }
  }
  
  return retval;
}

void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element)
{
  lf_queue_elem_t* node = (lf_queue_elem_t*)element;

  
  aba_ptr_t next;
  aba_ptr_t tail;

  aba_ptr_t temp;

  assert(queue != NULL);
  assert(node != NULL);

  node->next.ptr = 0;  //is this needed?

  
  while(true){

    tail = queue->tail;                //read tail.ptr and tail.aba together
    next = LF_ELEM_PTR(tail.ptr)->next;  //read next.ptr and next.aba together
    
    if ( eq(tail, queue->tail) ){  // are tail and next consistent ?
      // was tail pointing to the last node?
      if ( next.ptr == 0 ){
	// try to link node at the end of the linked list
	temp.ptr = (uintptr_t)node;
	temp.aba = next.aba + 1;
	if ( cas_64((volatile uint64_t *)&(LF_ELEM_PTR(tail.ptr)->next),
		     *((uint64_t *)&next),
		     *((uint64_t *)&temp)) ){
	  break;
	} 
      } else {
	//tail was not pointing to the last node
	//try to swing tail to the next node
	temp.ptr = next.ptr;
	temp.aba = tail.aba + 1;
	cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
      }
      
    }
  }
  // enqueue is done, Try to swing tail to the inserted node
  temp.ptr = (uintptr_t)node;
  temp.aba = tail.aba + 1;
  cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
  

}

