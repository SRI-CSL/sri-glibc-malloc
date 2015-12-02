/*
 * Small test
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "dsmalloc.h"

#define BNK_SIZE 37

int main(void) {
  char **bank;
  char *test;
  uint32_t i, step;
  size_t sz;

  bank = (char **) malloc(BNK_SIZE * sizeof(char*));
  fprintf(stderr, "bank = malloc(%d): got %p\n", BNK_SIZE, bank);
  if (bank == NULL) {
    fprintf(stderr, "malloc(%d) failed\n", BNK_SIZE);
    return 1;
  }

  for (sz = 5; sz < 100000; sz *= 4) {
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
  }
  
  for (sz = 1000000; sz > 10; sz /= 3) {
    for (i=0; i<BNK_SIZE; i++) {
      test = (char *) realloc(bank[i], sz);
      if (test == NULL) {
	fprintf(stderr, "malloc(%zu) failed (i = %"PRIu32")\n", sz, i);
	return 1;
      }
      bank[i] = test;
    }
    printf("Reallocated %"PRIu32" blocks of size %lu\n", i, (unsigned long) sz);
  }

  for (step = 2; step < 5; step ++) {
    for (i=0; i<BNK_SIZE; i+=step) {
      sz = (size_t) (random() % 2000000);
      if (bank[i] != NULL) {
	free(bank[i]);
      }
      test = (char *) malloc(sz);
      if (test == NULL) {
	fprintf(stderr, "malloc(%zu) failed (i = %"PRIu32")\n", sz, i);
	return 1;
      }
      bank[i] = test;
    }
  }

  for (i=0; i<BNK_SIZE; i++) {
    free(bank[i]);
  }
  
  free(bank);

  malloc_stats();

  return 0;
}
