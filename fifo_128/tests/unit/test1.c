#include <stdio.h>

#include "tests.h"

#define OODLES 1024

int main(int argc, char* argv[]){
  int i;
  bool success = true;
  
  lf_fifo_queue_t queue   __attribute__ ((aligned (16)));
  
  elem_t  oodles[OODLES]   __attribute__ ((aligned (16)));
  elem_t* node;
  
  fprintf(stderr, "&%s = %p\n", "oodles", &oodles);
  fprintf(stderr, "&%s = %p\n", "queue", &queue);
  
  lf_fifo_queue_init(&queue);

  for(i = 0; i < OODLES; i++){
    oodles[i].number = i;
    lf_fifo_enqueue(&queue, &oodles[i]);
  }

  for(i = 0; i < OODLES; i++){
    node = lf_fifo_dequeue(&queue);
    if(i != node->number){
      fprintf(stderr, "node %d's number is %llu\n", i, node->number);
      success = false;
    }  
  }
  
  if( !success ) exit(EXIT_FAILURE);
  
  fprintf(stdout, "[SUCCESS]\n");
  
  exit(EXIT_SUCCESS);
  
}
