/*
 * Small test
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "dsmalloc.h"

/* if compiled with -DUSE_DL_PREFIX these flip the malloc routines over to the "dl" versions. */
#include "switch.h"

#define BNK_SIZE 37

int main(void) {
  char **bank;
  char *test;
  uint32_t i;
  size_t sz;

  bank = (char **) malloc(BNK_SIZE * sizeof(char*));
  fprintf(stderr, "bank = malloc(%d): got %p\n", BNK_SIZE, bank);
  if (bank == NULL) {
    fprintf(stderr, "malloc(%d) failed\n", BNK_SIZE);
    return 1;
  }

  sz = 5;
  for (i=0; i<BNK_SIZE; i++) {
    test = (char *) malloc(sz);
    if (test == NULL) {
      fprintf(stderr, "malloc(%zu) failed (i = %"PRIu32")\n", sz, i);
      return 1;
    }
    bank[i] = test;
  }
  printf("Allocated %"PRIu32" blocks of size %lu\n", i, (unsigned long) sz);

  for (i=0; i<BNK_SIZE; i++) {
    free(bank[i]);
    bank[i] = NULL;
  }

  free(bank);

#ifdef USE_DL_PREFIX
  dlmalloc_stats();
#else
  malloc_stats();
#endif

  return 0;
}
