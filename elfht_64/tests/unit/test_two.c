#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <inttypes.h>

#include "lfht.h"

uint32_t max = 1024;

#define MAX_THREADS 128

static lfht_t tbl;

typedef struct targs {
  int id;
  int val;
  int count;
  int successes;
} targs_t;


void* thread_main(void* targ){
  targs_t* targsp = (targs_t*)targ;
  int i;

  for(i = 1; i <= targsp->count; i++){
    if( lfht_add(&tbl, i * 16, i) ){
      targsp->successes ++;
    }
  }

  pthread_exit(NULL);
}


int main(int argc, char* argv[]){
  int nthreads;
  int rc;
  int i;
  pthread_t threads[MAX_THREADS];
  targs_t targs[MAX_THREADS];
  void* status;
  int total = 0;
  bool success;

  if (argc != 3) {
    fprintf(stdout, "Usage: %s <nthreads> <count in K>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  nthreads = atoi(argv[1]);

  uint32_t count = 1024 * atoi(argv[2]);

  
  if((nthreads == 0) ||  (nthreads >= MAX_THREADS)){
    fprintf(stdout, "Usage: %s <nthreads>\n", argv[0]);
    fprintf(stdout, "\t(nthreads > 0) and (nthreads < %d)\n", MAX_THREADS);
    exit(EXIT_FAILURE);
  }

  success = init_lfht(&tbl, max);

  if( !success ) exit(EXIT_FAILURE);

  for(i = 0; i < nthreads; i++){
    targs_t *targsp = &targs[i];
    targsp->id = i;
    targsp->val = 1;
    targsp->count = count;
    targsp->successes = 0;
  }

   for( i = 0; i < nthreads; i++){

     rc = pthread_create(&threads[i], NULL, thread_main, (void *)&targs[i]);

     if (rc){
       fprintf(stderr, "return code from pthread_create() is %d\n", rc);
       exit(EXIT_FAILURE);
     }
     
   }
  
  for( i = 0; i < nthreads; i++){

    rc = pthread_join(threads[i], &status);
    if (rc){
      fprintf(stderr, "return code from pthread_join() is %d\n", rc);
      exit(EXIT_FAILURE);
    }
  }

  
  
  for(i = 0; i < nthreads; i++){
    total += targs[i].successes;
    //fprintf(stdout, "thread %d with %d successes\n", targs[i].id, targs[i].successes);
  }

  lfht_stats(stderr, "static", &tbl);

  success = delete_lfht(&tbl);

  if( !success ) exit(EXIT_FAILURE);

  if( total != count * nthreads){
    fprintf(stderr, "%d successes out of %d attempts\n", total, count * nthreads);
    exit(EXIT_FAILURE);
  }
  
  fprintf(stdout, "[SUCCESS]\n");

  exit(EXIT_SUCCESS);
}
