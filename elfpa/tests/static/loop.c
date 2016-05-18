#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

#include "malloc.h"


#define SZ 25568

static void* the_pointer = NULL;


static pthread_t        mallocer;
//static pthread_mutex_t  malloc_lock = PTHREAD_MUTEX_INITIALIZER;
//static pthread_cond_t   malloc_condvar = PTHREAD_COND_INITIALIZER;


static pthread_t        freer;
static pthread_mutex_t  free_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   free_condvar = PTHREAD_COND_INITIALIZER;



void* malloc_thread(void* arg){

  while(true){
    pthread_mutex_lock(&free_lock);

    the_pointer = malloc(SZ);
    fprintf(stderr, "malloced %p\n", the_pointer);
    fflush(stderr);
    
    pthread_cond_signal(&free_condvar);
    pthread_mutex_unlock(&free_lock);
    
    pthread_mutex_lock(&free_lock);
    while(the_pointer != NULL){
      pthread_cond_wait(&free_condvar, &free_lock);
    }
    
    pthread_mutex_unlock(&free_lock);
    
    the_pointer = malloc(SZ);
    fprintf(stderr, "malloced %p\n", the_pointer);
    fflush(stderr);
    
  }
  
  pthread_exit(NULL);
}

void* free_thread(void* arg){

  while(true){

    pthread_mutex_lock(&free_lock);
    while(the_pointer == NULL){
      pthread_cond_wait(&free_condvar, &free_lock);
    }
    
    free(the_pointer);
    fprintf(stderr, "freed %p\n", the_pointer);
    fflush(stderr);
    
    the_pointer = NULL;
    
    pthread_cond_signal(&free_condvar);
    
    pthread_mutex_unlock(&free_lock);
  }
   
  pthread_exit(NULL);
}


int main(int argc, char* argv[]){


  pthread_create(&freer, NULL, free_thread, NULL);
 
  pthread_create(&mallocer, NULL, malloc_thread, NULL);
 

  pthread_join(freer, NULL);

  pthread_join(mallocer, NULL);

  return 0;
}
