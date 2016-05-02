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
  end.next.top = 0;
  end.next.count = 0;
  queue->head.top = (uintptr_t)&end;
  queue->tail.top = (uintptr_t)&end;

}

static inline bool eq(pointer_t* lhs, volatile pointer_t* rhs){
  //return lhs->top == rhs->top && lhs->count == rhs->count;
  return *((uint64_t *)lhs) == *((uint64_t *)rhs);
}


void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{
  pointer_t head;
  pointer_t tail;
  pointer_t next;

  pointer_t temp;

  lf_queue_elem_t* retval;
  
  assert(queue != NULL);

  retval = NULL;
  
  while(true){

    head = queue->head;       //read the head
    tail = queue->tail;       //read the tail
    next = ((lf_queue_elem_t *)head.top)->next;    //read the head.top->next

    if( eq(&head, &(queue->head)) ){ //are head, tail, and next consistent

      if( head.top == tail.top ){   //is queue empty or tail falling behind

	if(next.top == 0){
	  return NULL;
	}
	//tail is falling behind. Try to advance it.
	temp.top = next.top;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail,
		*((uint64_t *)&tail),
		*((uint64_t *)&temp));
	
      } else { //no need to deal with tail.

	//read the value before the cas
	//otherwise, another dequeue might free the next node
	retval = (lf_queue_elem_t *)next.top;
	//try to swing head to the next node
	temp.top = next.top;
	temp.count = head.count + 1;

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

  
  pointer_t next;
  pointer_t tail;

  pointer_t temp;

  assert(queue != NULL);
  assert(node != NULL);

  node->next.top = 0;  //is this needed?

  
  while(true){

    tail = queue->tail;                //read tail.top and tail.count together
    next = ((lf_queue_elem_t*)tail.top)->next;  //read next.top and next.count together
    
    if ( eq(&tail, &(queue->tail)) ){  // are tail and next consistent ?
      // was tail pointing to the last node?
      if ( next.top == 0 ){
	// try to link node at the end of the linked list
	temp.top = (uintptr_t)node;
	temp.count = next.count + 1;
	if ( cas_64((volatile uint64_t *)&(((lf_queue_elem_t*)tail.top)->next),
		     *((uint64_t *)&next),
		     *((uint64_t *)&temp)) ){
	  break;
	} 
      } else {
	//tail was not pointing to the last node
	//try to swing tail to the next node
	temp.top = next.top;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
      }
      
    }
  }
  // enqueue is done, Try to swing tail to the inserted node
  temp.top = (uintptr_t)node;
  temp.count = tail.count + 1;
  cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
  

}

