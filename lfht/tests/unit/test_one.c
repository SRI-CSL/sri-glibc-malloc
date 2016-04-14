#include <stdlib.h>
#include "lfht.h"


uint32_t max = 16 * 4096;

static lfht_t ht;

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  


int main(int argc, char* argv[]){
  uint32_t i;
  bool success;


  success = init_lfht(&ht, max);

  if( !success ) exit(EXIT_FAILURE);

  for(i = 1; i <= max; i++){
    if( ! lfht_insert(&ht, i, i) ){
      fprintf(stderr, "%s failed for i = %d\n", argv[0], i);
      exit(EXIT_FAILURE);
    }
  }


  success = delete_lfht(&ht);

  if( !success ) exit(EXIT_FAILURE);


  exit(EXIT_SUCCESS);

}


