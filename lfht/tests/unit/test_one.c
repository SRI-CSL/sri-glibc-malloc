#include <stdlib.h>
#include "lfht.h"


uint32_t max = 16 * 4096;

uint32_t count = 4096;


static lfht_t ht;


extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  


int main(int argc, char* argv[]){
  uint32_t i;
  bool success;
  uintptr_t val;

  success = init_lfht(&ht, max);

  if( !success ) exit(EXIT_FAILURE);

  for(i = 1; i <= count; i++){
    if( ! lfht_insert(&ht, i, i) ){
      fprintf(stderr, "%s insert failed for i = %d\n", argv[0], i);
      exit(EXIT_FAILURE);
    }
  }

  for(i = 1; i <= count; i++){
    if( ! lfht_find(&ht, i, &val) ){
      fprintf(stderr, "%s find failed for i = %d\n", argv[0], i);
      exit(EXIT_FAILURE);
    }
    if(val != i){
      fprintf(stderr, "%s find integrity failed for i = %d\n", argv[0], i);
      exit(EXIT_FAILURE);
    }
  }


  

  success = delete_lfht(&ht);

  if( !success ) exit(EXIT_FAILURE);


  exit(EXIT_SUCCESS);

}


