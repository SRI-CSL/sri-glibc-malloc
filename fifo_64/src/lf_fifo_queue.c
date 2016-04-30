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

static node_t end  __attribute__ ((aligned (16)));

void lf_fifo_queue_init(lf_fifo_queue_t *queue)
{
  assert(queue != NULL);
  end.next.ptr = 0;
  end.next.count = 0;
  queue->head.ptr = (uintptr_t)&end;
  queue->tail.ptr = (uintptr_t)&end;

}

static inline bool eq(pointer_t* lhs, volatile pointer_t* rhs){
  return lhs->ptr == rhs->ptr && lhs->count == rhs->count;
}


void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{
  pointer_t head;
  pointer_t tail;
  pointer_t next;

  pointer_t temp;

  node_t* retval;
  
  assert(queue != NULL);

  retval = NULL;
  
  while(true){

    head = queue->head;       //read the head
    tail = queue->tail;       //read the tail
    next = ((node_t *)head.ptr)->next;    //read the head.ptr->next

    if( eq(&head, &(queue->head)) ){ //are head, tail, and next consistent

      if( head.ptr == tail.ptr ){   //is queue empty or tail falling behind

	if(next.ptr == 0){
	  return NULL;
	}
	//tail is falling behind. Try to advance it.
	temp.ptr = next.ptr;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail,
		*((uint64_t *)&tail),
		*((uint64_t *)&temp));
	
      } else { //no need to deal with tail.

	//read the value before the cas
	//otherwise, another dequeue might free the next node
	retval = (node_t *)next.ptr;
	//try to swing head to the next node
	temp.ptr = next.ptr;
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
  node_t* node = (node_t*)element;

  
  pointer_t next;
  pointer_t tail;

  pointer_t temp;

  assert(queue != NULL);
  assert(node != NULL);

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
	if ( cas_64((volatile uint64_t *)&(((node_t*)tail.ptr)->next),
		     *((uint64_t *)&next),
		     *((uint64_t *)&temp)) ){
	  break;
	} 
      } else {
	//tail was not pointing to the last node
	//try to swing tail to the next node
	temp.ptr = next.ptr;
	temp.count = tail.count + 1;
	cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
      }
      
    }
  }
  // enqueue is done, Try to swing tail to the inserted node
  temp.ptr = (uintptr_t)node;
  temp.count = tail.count + 1;
  cas_64((volatile uint64_t *)&queue->tail, *((uint64_t *)&tail), *((uint64_t *)&temp));
  

}

