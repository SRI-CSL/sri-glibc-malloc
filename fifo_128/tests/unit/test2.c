#include <stdio.h>
#include <pthread.h>

#include "tests.h"

#define OODLES 1024 * 1024

static lf_fifo_queue_t queue   __attribute__ ((aligned (16)));
  
static  elem_t  oodles[OODLES]   __attribute__ ((aligned (16)));

void* thread_main(void* targ){
  bool evens = (bool)targ;
  uint32_t rem = evens ? 0 : 1;
  uint32_t i;
  fprintf(stderr, "thread %d\n", evens);
  for(i = 0; i < OODLES; i++){
    if( i % 2 == rem ){
      //fprintf(stderr, "thread %d: %d\n", evens, i);
      lf_fifo_enqueue(&queue, &oodles[i]);
    }
  }

  pthread_exit(NULL);
}



int main(int argc, char* argv[]){
  int i, rc;

  bool success = true;

  pthread_t threads[2];
  bool targs[2] = {true, false};
  void *status;
  elem_t* node;
  
  fprintf(stderr, "&%s = %p\n", "oodles", &oodles);
  fprintf(stderr, "&%s = %p\n", "queue", &queue);
  
  lf_fifo_queue_init(&queue);

  for(i = 0; i < OODLES; i++){
    oodles[i].number = i;
  }


  for( i = 0; i < 2; i++){

    rc = pthread_create(&threads[i], NULL, thread_main, (void *)targs[i]);

    if (rc){
      fprintf(stderr, "return code from pthread_create() is %d\n", rc);
      exit(EXIT_FAILURE);
    }
    
  }
  
  for( i = 0; i < 2; i++){
    
    rc = pthread_join(threads[i], &status);
    if (rc){
      fprintf(stderr, "return code from pthread_join() is %d\n", rc);
      exit(EXIT_FAILURE);
    }
  }

  for(i = 0; i < OODLES; i++){
    node = lf_fifo_dequeue(&queue);
    if(node != NULL){
      node->number = 0;
    } else {
      fprintf(stderr, "%d call failed\n", i);
    }
  }

  for(i = 0; i < OODLES; i++){
    if(oodles[i].number != 0){
      fprintf(stderr, "elem[%d].number != 0\n", i);
      success = false;
    }
  }
  
  if( !success ) exit(EXIT_FAILURE);
  
  fprintf(stdout, "[SUCCESS]\n");
  
  exit(EXIT_SUCCESS);
  
}
