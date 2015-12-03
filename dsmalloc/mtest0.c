/*
 * Small test
 */

#include <stdlib.h>
#include <stdio.h>

#include "dsmalloc.h"

/* if compiled with -DUSE_DL_PREFIX these flip the malloc routines over to the "dl" versions. */
#include "switch.h"

int main(void) {
  char *test = malloc(1001);
  fprintf(stderr, "test = malloc(1001): got %p\n", test);
  free(test);
  malloc_stats();

  return 0;
}
