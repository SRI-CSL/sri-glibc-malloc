#include <ck_ht.h>
#include <stdio.h>
#include <stdlib.h>

#include "ckallocator.h"
#include "cktable.h"

static size_t max = 1024 * 1024;

//our allocator
static struct ck_malloc allocator;

int main(int argc, char *argv[]){
  size_t i;


  //our ck hash table
  ck_ht_t ht CK_CC_CACHELINE;


  if(!ck_allocator_init(&allocator)){
    exit(EXIT_FAILURE);
  }

  if (table_init(&allocator, &ht, max) == false) {
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Creation stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
    bool success = table_insert(&ht, (void*)i, (void*)i+1);
    if( ! success ){
      fprintf(stderr, "Insertion failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Insertion stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
     uintptr_t val = (uintptr_t)table_get(&ht, (void*)i);
    if( val != i + 1){
      fprintf(stderr, "Retrieval failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Retrieval stage complete. Table size = %zu\n", table_count(&ht));


  for(i = 1; i <= max; i++){
    bool success = table_replace(&ht, (void*)i, (void*)i+2);
    if( ! success ){
      fprintf(stderr, "Insertion failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Update stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
    uintptr_t val = (uintptr_t)table_get(&ht, (void*)i);
    if( val != i + 2){
      fprintf(stderr, "Retrieval of update failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Update retrieval stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
     bool success = table_remove(&ht, (void*)i);
    if( ! success ){
      fprintf(stderr, "Removal failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Removal stage complete. Table size = %zu\n", table_count(&ht));

  table_reset(&ht);

  fprintf(stderr, "OK %zu\n", ck_allocated());

  return 0;
}


