#include <stdio.h>

#include "lf_fifo_queue.h"

typedef struct elem_s {
  volatile node_t node;
  uint64_t number;
} elem_t;


elem_t* elem_alloc(){ return malloc(sizeof(elem_t)); }




int main(int argc, char* argv[]){

  lf_fifo_queue_t queue;
  
  elem_t  one   __attribute__ ((aligned (16)));
  elem_t  two   __attribute__ ((aligned (16)));
  elem_t  three  __attribute__ ((aligned (16)));

  one.number   = 1;
  two.number   = 2;
  three.number = 3;

  fprintf(stderr, "sizeof(%s) = %zu\n", "pointer_t", sizeof(pointer_t));
  fprintf(stderr, "sizeof(%s) = %zu\n", "node_t", sizeof(node_t));

  fprintf(stderr, "&%s = %p\n", "one", &one);
  fprintf(stderr, "&%s = %p\n", "two", &two);
  fprintf(stderr, "&%s = %p\n", "three", &three);
  
  lf_fifo_queue_init(&queue);

  lf_fifo_enqueue(&queue, &one);

  lf_fifo_enqueue(&queue, &two);

  lf_fifo_enqueue(&queue, &three);


  for(int i = 0; i < 3; i++){
    elem_t* node = lf_fifo_dequeue(&queue);
    fprintf(stderr, "node %d's number is %llu\n", i, node->number);
  }  



}
