/*
 * Small test
 */

#include <stdlib.h>
#include <stdio.h>

#include "malloc.h"

int main(void) {
  char *test = malloc(1001);
  fprintf(stderr, "test = malloc(1001): got %p\n", test);
  free(test);
  malloc_stats();

  return 0;
}
