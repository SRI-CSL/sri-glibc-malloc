#include "atomic.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <inttypes.h>

/*
  Make sure our  compare_and_swap128 appears to work ok.
  
*/

#define MAX_THREADS  1024

static aba_128_t abba;

bool update(aba_128_t* abbap, int val){
  aba_128_t old;
  aba_128_t new;
  
  /* attempt to read the "current" values */
  
  uintptr_t ptr = __atomic_load_n(&abbap->ptr, __ATOMIC_SEQ_CST);
  uint64_t  tag = __atomic_load_n(&abbap->tag, __ATOMIC_SEQ_CST);

  old.ptr = ptr;
  old.tag = tag;
  
  ptr  += val;
  tag  += val;

  new.ptr = ptr;
  new.tag = tag;

  /* should fail every now and then... */
  return compare_and_swap128(abbap, old, new);
}

typedef struct targs {
  int id;
  int val;
  int count;
  int successes;
} targs_t;


void* thread_main(void* targ){
  targs_t* targsp = (targs_t*)targ;
  int i;

  for(i = 0; i < targsp->count; i++){
    targsp->successes += update(&abba, targsp->val);
  }

  pthread_exit(NULL);
}




int main(int argc, char* argv[]){
  int nthreads;
  int rc;
  int i;
  int total;
  pthread_t threads[MAX_THREADS];
  targs_t targs[MAX_THREADS];
  void* status;
  

  if (argc != 2) {
    fprintf(stdout, "Usage: %s <nthreads>\n", argv[0]);
    return 1;
  }

  nthreads = atoi(argv[1]);

  if((nthreads == 0) ||  (nthreads >= MAX_THREADS)){
    fprintf(stdout, "Usage: %s <nthreads>\n", argv[0]);
    fprintf(stdout, "\t(nthreads > 0) and (nthreads < %d)\n", MAX_THREADS);
    return 1;
  }

  
  abba.ptr = 0;
  abba.tag = 0;

  for(i = 0; i < nthreads; i++){
    targs_t *targsp = &targs[i];
    targsp->id = i;
    targsp->val = 1;
    targsp->count = 1000;
    targsp->successes = 0;
  }

   for( i = 0; i < nthreads; i++){

     rc = pthread_create(&threads[i], NULL, thread_main, (void *)&targs[i]);

     if (rc){
       fprintf(stderr, "return code from pthread_create() is %d\n", rc);
       exit(-1);
     }
     
   }
  
  for( i = 0; i < nthreads; i++){

    rc = pthread_join(threads[i], &status);
    if (rc){
      fprintf(stderr, "return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
  }



  
  for(i = 0; i < nthreads; i++){
    total += targs[i].successes;
    fprintf(stdout, "thread %d with %d successes\n", targs[i].id, targs[i].successes);
  }

  /* should all be equal */
  fprintf(stdout, "total = %d abba.ptr = %"PRIu64" abba.tag = %"PRIu64"\n", total, abba.ptr,  abba.tag);
 

  return 0;
}
