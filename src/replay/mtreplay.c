/*
 * Copyright (C) 2016  SRI International
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


#include "malloc.h"

#include "replaylib.h"

#define MAX_THREADS  1024

/* flag to turn on malloc_stats at the end */
static const bool verbose = true;

/*
 *  Nothing fancy yet.
 *  Given nthreads and an mhook script it creates
 *  that number of threads, each replaying the script.
 *
 *  Hopefully in the exact same fashion (but whether our calls to
 *  realloc match the scripts lies in the lap of the malloc gods). 
 *
 *
 */

typedef struct targs {
  int id;
  const char* filename;
} targs_t;




void* thread_main(void* targ){
  targs_t* targsp = (targs_t*)targ;
  process_file(targsp->filename, verbose);
  pthread_exit(NULL);
}


int main(int argc, char* argv[]){
  int nthreads;
  int rc;
  int i;
  pthread_t threads[MAX_THREADS];
  targs_t targs[MAX_THREADS];
  void* status;
  
  if (argc != 3) {
    fprintf(stdout, "Usage: %s <nthreads> <mhook output file>\n", argv[0]);
    return 1;
  }

  nthreads = atoi(argv[1]);


  if(nthreads <= 0){

    process_file(argv[2], true);


  } else if ( nthreads >= MAX_THREADS){
    fprintf(stdout, "nthreads must be less than %d\n", MAX_THREADS);
    return 1;
  } else {

   
   for( i = 0; i < nthreads; i++){

     targs[i].id = i;

     targs[i].filename = argv[2];

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
   
  }
  
  if (true || verbose) {
    malloc_stats();
  }
  
  
  pthread_exit(NULL);

}
