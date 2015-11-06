#include "linhash.h"

#include <stdlib.h>

linhash_t numerouno;


static void test_0(void);
static void test_1(void);
static void test_2(void);



int main(int argc, char** argv){
  int tests[] = { 0, 0, 1 };

  if(tests[0]){ test_0(); }

  if(tests[1]){ test_1(); }

  if(tests[2]){ test_2(); }
 
  
  return 0;
}


void test_0(void){
  int key, value;
  void* look;
  bool success;
  
  init_linhash(&numerouno, sys_memcxt);

  fprintf(stderr, "key = %p  value = %p\n", &key, &value);
  
  linhash_insert(&numerouno, &key, &value);

  look = linhash_lookup(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p look = %p\n", &key, &value, look);

  dump_linhash(stderr, &numerouno, true);

  success = linhash_delete(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p success = %d\n", &key, &value, success);

  dump_linhash(stderr, &numerouno, true);

  success = linhash_delete(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p success = %d\n", &key, &value, success);

  dump_linhash(stderr, &numerouno, true);

  delete_linhash(&numerouno);
}


const size_t K = 1024;
const size_t K2 = K << 1;
const size_t K4 = K2 << 1;

void test_1(void){
  size_t index;
  
  void* zoo = calloc(K2, sizeof(char));
  
  init_linhash(&numerouno, sys_memcxt);

  for(index = 0; index < K2; index++){
    linhash_insert(&numerouno, &zoo[index], &zoo[index]);
  }

  dump_linhash(stderr, &numerouno, true);
    
  for(index = 0; index < K2; index++){
    linhash_delete(&numerouno, &zoo[index]);
  }
  
  dump_linhash(stderr, &numerouno, true);

  delete_linhash(&numerouno);

  free(zoo);
  
}


void test_2(void){

  size_t index;
  size_t zindex;

  void ** menagery;
  

  init_linhash(&numerouno, sys_memcxt);

  menagery = calloc(K4, sizeof(void *));

  
  for(zindex = 0; zindex < K4; zindex++){
  
    void* zoo = calloc(K4, sizeof(char));
  
    for(index = 0; index < K4; index++){
      linhash_insert(&numerouno, &zoo[index], &zoo[index]);
    }

    menagery[zindex] = zoo;

  }

  dump_linhash(stderr, &numerouno, false);

  for(zindex = 0; zindex < K4; zindex++){

    void* zoo = menagery[zindex];
  
    for(index = 0; index < K4; index++){
      linhash_delete(&numerouno, &zoo[index]);
    }

    menagery[zindex] = NULL;

    free(zoo);

  }
  
  dump_linhash(stderr, &numerouno, false);

  delete_linhash(&numerouno);

  free(menagery);
  
}
