#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "atomic.h"
#include "lf_fifo_queue.h"


int main(int argc, char* argv[]){
  char p[16] = "";
  int i;
  pointer_t pointer;

 fprintf(stderr, "sizeof(pointer_t) = %zu\n", sizeof(pointer_t));	

  for(i = 0; i < 16; i++){
    pointer.ptr = (unsigned long)&p[i];
    pointer.count = 3;
    fprintf(stderr, "&p[%d]       = %p\n", i, &p[i]);	 
    fprintf(stderr, "pointer.ptr  = Ox%llx\n", pointer.ptr);	 
    fprintf(stderr, "pointer.count = %d\n", pointer.count);	 
  }

  return 0;
}
