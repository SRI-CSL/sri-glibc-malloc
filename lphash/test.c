#include "lphash.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

lphash_t numerouno;


static void test_0(void);
static void test_1(void);
static void test_2(void);
static void test_3(void);

#define K   1024
#define K2  (K << 1)
#define K4  (K2 << 1)

/* Multiply two size_t values, checking for overflow */
static bool mul_size(size_t s1, size_t s2, size_t* prod){
  size_t result;

  assert(prod != NULL);

  if (s1 == 0 || s2 == 0){
    *prod = 0;
    return true;
  }
  result = s1 * s2;
  if (result / s2 != s1){
    return false;
  }
  
  *prod = result;
  return result;
}


int main(int argc, char** argv){

  int tests[] = { 0, 0, 1, 0};


  fprintf(stderr, "Using %s\n",  (argc > 1) ? "sys_memcxt" : "pool_memcxt");

  if(tests[0]){ test_0(); }
  
  if(tests[1]){ test_1(); }

  if(tests[2]){ test_2(); }

  if(tests[3]){ test_3(); }
  
  return 0;
}


void test_0(){
  int key, value;
  void* look;
  bool success;
  
  init_lphash(&numerouno);

  fprintf(stderr, "inserting key = %p  value = %p\n", &key, &value);
  
  lphash_insert(&numerouno, &key, &value);

  look = lphash_lookup(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p look = %p\n", &key, &value, look);

  dump_lphash(stderr, &numerouno, true);

  success = lphash_delete(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p success = %d\n", &key, &value, success);

  dump_lphash(stderr, &numerouno, true);

  success = lphash_delete(&numerouno, &key);

  fprintf(stderr, "key = %p  value = %p success = %d\n", &key, &value, success);

  dump_lphash(stderr, &numerouno, true);

  delete_lphash(&numerouno);

}



void test_1(){
  bool found;
  size_t index;
  
  void* zoo = calloc(K2, sizeof(char));
  
  init_lphash(&numerouno);

  for(index = 0; index < K2; index++){
    lphash_insert(&numerouno, zoo + index, zoo + index);
  }

  dump_lphash(stderr, &numerouno, true);
    
  for(index = 0; index < K2; index++){
    found = lphash_delete(&numerouno, zoo + index);
    if(!found){
      fprintf(stderr, "index = %" PRIuPTR "\n", index);
    }
    assert(found);
  }
  
  dump_lphash(stderr, &numerouno, true);

  delete_lphash(&numerouno);

  free(zoo);
  
}


void test_2(){
  bool found;
  size_t index;
  size_t zindex;

  void ** menagery;
  

  init_lphash(&numerouno);

  dump_lphash(stderr, &numerouno, false);

  menagery = calloc(K4, sizeof(void *));
  
  for(zindex = 0; zindex < K4; zindex++){

    if(0 && (zindex % K == 0)){
      fprintf(stderr, "zindex = %" PRIuPTR "\n", zindex);
    }
    
    void* zoo = calloc(K4, sizeof(char));
  
    for(index = 0; index < K4; index++){

      if(0 && (zindex % K == 0) && (index % K == 0)){
	fprintf(stderr, "zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
      }
      
      lphash_insert(&numerouno, zoo + index, zoo + index);
    }

    menagery[zindex] = zoo;

  }

  dump_lphash(stderr, &numerouno, false);

  for(zindex = 0; zindex < K4; zindex++){

    if(0 && (zindex % K == 0)){
      fprintf(stderr, "> zindex = %" PRIuPTR "\n", zindex);
    }

    void* zoo = menagery[zindex];
  
    for(index = 0; index < K4; index++){

      if(0 && (zindex % K == 0) && (index % K == 0)){
	fprintf(stderr, "zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
      }

      found = lphash_delete(&numerouno, zoo + index);
      if(!found){
	fprintf(stderr, "zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
      }
      assert(found);
    }

    
    menagery[zindex] = NULL;
    
    
    free(zoo);

    if(!found){ break; }

  }
  
  dump_lphash(stderr, &numerouno, false);

  delete_lphash(&numerouno);

  free(menagery);
  
}


void test_3(){
  bool found;
  size_t index;
  size_t zindex;
  size_t alot;
  size_t alsoalot;
  size_t lots_n_lots;
  bool success;

  void **menagery;
  
  const int32_t exp0 = 16;
  const int32_t exp1 = 12;

  init_lphash(&numerouno);

  fprintf(stderr, "exp0 = %d exp1 = %d\n", exp0, exp1);

  fprintf(stderr, "bincount_max = %" PRIuPTR "\n", numerouno.cfg.bincount_max);

  alot = ((uint64_t)1) << exp0;

  alsoalot = ((uint64_t)1) << exp1;

  success = mul_size(alot, alsoalot, &lots_n_lots);
    
  if(!success){
    return;
  }
    
  fprintf(stderr, "keys         = %" PRIuPTR "\n", lots_n_lots);
    
  menagery = calloc(alot, sizeof(void *));
  if(menagery != NULL){
    
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "+");
      }
      
      void* zoo = calloc(alsoalot, sizeof(char));
      if(zoo != NULL){
	
	for(index = 0; index < alsoalot; index++){
	 found = lphash_insert(&numerouno, zoo + index, zoo + index);
	 if(!found){
	    fprintf(stderr, "lphash_insert FAILED: zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
	    break;
	  }
	 assert(found);

	}
      }
      
      menagery[zindex] = zoo;
      
    }
  }
  
  fprintf(stderr, "\n");
  
  dump_lphash(stderr, &numerouno, false);

  if(menagery != NULL){
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "-");
      }
      
      void* zoo = menagery[zindex];
      if(zoo != NULL){
      
	for(index = 0; index < alsoalot; index++){
	  
	  found = lphash_delete(&numerouno, zoo + index);
	  if(!found){
	    fprintf(stderr, "lphash_delete FAILED: zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
	    break;
	  }
	  assert(found);
	}
      }
      
      menagery[zindex] = NULL;
      
      free(zoo);
      
    }

  }
  fprintf(stderr, "\n");
  
  dump_lphash(stderr, &numerouno, false);
  
  delete_lphash(&numerouno);
  
  free(menagery);
    
}


