#include <stdio.h>
#include "malloc.h"

int main(int argc, char* argv[]){
  void* ptr = malloc(1024);
  fprintf(stderr, "Hello world\n");
  free(ptr);
  return 0;
}

