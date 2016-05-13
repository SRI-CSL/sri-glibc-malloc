#include "lf_queue.h"

void lf_queue_init(lf_queue_t *queue)
{
	queue->both.ptr = 0;
	queue->both.aba = 0;
}


void *lf_dequeue(lf_queue_t *queue)
{
	aba_ptr_t head;
	aba_ptr_t next;

	while(1) {
		head.ptr = queue->both.ptr;
		head.aba = queue->both.aba;
		if (head.ptr == 0)
			return NULL;
		next.ptr = (uintptr_t)LF_ELEM_PTR(head.ptr)->next.ptr;
		next.aba = head.aba + 1;
		if (cas_64((volatile uint64_t *)&(queue->both),
			   *((uint64_t*)&head),
			   *((uint64_t*)&next))) {
			return (void *)LF_ELEM_PTR(head.ptr);
		}
	}
}
int lf_enqueue(lf_queue_t *queue, void *element)
{
	aba_ptr_t old_top;
	aba_ptr_t new_top;
	//aba_ptr_t next;
	
	while(1) {
	  //old_top.aba = queue->both.aba;
	  //old_top.ptr = queue->both.ptr;
	  old_top = queue->both;
	  //	  next = LF_ELEM_PTR(old_top.ptr);
	  //	  ((struct queue_elem_t *)element)->next = next;
	  ((struct queue_elem_t *)element)->next = old_top;
	  new_top.ptr = (unsigned long)element;
	  new_top.aba = old_top.aba + 1;
	  if (cas_64((volatile uint64_t *)&(queue->both),
		     *((uint64_t *)&old_top),
		     *((uint64_t *)&new_top))) {
	    return 0;
	  }
	}
}

