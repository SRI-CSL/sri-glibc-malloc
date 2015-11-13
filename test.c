#include "linhash.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#include "pool.h"
#include "memcxt.h"

linhash_t numerouno;


static void test_0(memcxt_t* memcxt);
static void test_1(memcxt_t* memcxt);
static void test_2(memcxt_t* memcxt);
static void test_3(memcxt_t* memcxt);

#define K   1024
#define K2  K << 1
#define K4  K2 << 1


int main(int argc, char** argv){
  memcxt_t* memcxt;
  int tests[] = { 0, 0, 0, 1};

  memcxt = (argc > 1) ? sys_memcxt : pool_memcxt;

  fprintf(stderr, "Using %s\n",  (argc > 1) ? "sys_memcxt" : "pool_memcxt");

  if(tests[0]){ test_0(memcxt); }
  
  if(tests[1]){ test_1(memcxt); }

  if(tests[2]){ test_2(memcxt); }

  if(tests[3]){ test_3(memcxt); }
  
  return 0;
}


void test_0(memcxt_t* memcxt){
  int key, value;
  void* look;
  bool success;
  
  init_linhash(&numerouno, memcxt);

  fprintf(stderr, "inserting key = %p  value = %p\n", &key, &value);
  
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

  dump_pool(stderr);
 
}



void test_1(memcxt_t* memcxt){
  bool found;
  size_t index;
  
  void* zoo = calloc(K2, sizeof(char));
  
  init_linhash(&numerouno, memcxt);

  for(index = 0; index < K2; index++){
    linhash_insert(&numerouno, zoo + index, zoo + index);
  }

  dump_linhash(stderr, &numerouno, true);
    
  for(index = 0; index < K2; index++){
    found = linhash_delete(&numerouno, zoo + index);
    if(!found){
      fprintf(stderr, "index = %zu\n", index);
    }
    assert(found);
  }
  
  dump_linhash(stderr, &numerouno, true);

  delete_linhash(&numerouno);

  free(zoo);
  
}


void test_2(memcxt_t* memcxt){
  bool found;
  size_t index;
  size_t zindex;

  void ** menagery;
  

  init_linhash(&numerouno, memcxt);

  menagery = calloc(K4, sizeof(void *));

  
  for(zindex = 0; zindex < K4; zindex++){

    if(0 && (zindex % K == 0)){
      fprintf(stderr, "zindex = %zu\n", zindex);
    }
    
    void* zoo = calloc(K4, sizeof(char));
  
    for(index = 0; index < K4; index++){

      if(0 && (zindex % K == 0) && (index % K == 0)){
	fprintf(stderr, "zindex = %zu index = %zu\n", zindex, index);
      }
      
      linhash_insert(&numerouno, zoo + index, zoo + index);
    }

    menagery[zindex] = zoo;

  }

  dump_linhash(stderr, &numerouno, false);

  for(zindex = 0; zindex < K4; zindex++){

    if(0 && (zindex % K == 0)){
      fprintf(stderr, "> zindex = %zu\n", zindex);
    }

    void* zoo = menagery[zindex];
  
    for(index = 0; index < K4; index++){

      if(0 && (zindex % K == 0) && (index % K == 0)){
	fprintf(stderr, "zindex = %zu index = %zu\n", zindex, index);
      }

      found = linhash_delete(&numerouno, zoo + index);
      if(!found){
	fprintf(stderr, "zindex = %zu index = %zu\n", zindex, index);
      }
      assert(found);
    }

    
    menagery[zindex] = NULL;
    
    
    free(zoo);

    if(!found){ break; }

  }
  
  dump_linhash(stderr, &numerouno, false);

  delete_linhash(&numerouno);

  free(menagery);
  
}

void test_3(memcxt_t* memcxt){
  bool found;
  size_t index;
  size_t zindex;
  size_t alot;
  size_t alsoalot;
  size_t lots_n_lots;
  bool success;

  void **menagery;
  
  const int32_t exp0 = 16;
  const int32_t exp1 = 14;

  init_linhash(&numerouno, memcxt);

  fprintf(stderr, "exp0 = %d exp1 = %d\n", exp0, exp1);

  fprintf(stderr, "bincount_max = %zu\n", numerouno.cfg.bincount_max);

  alot = ((uint64_t)1) << exp0;

  alsoalot = ((uint64_t)1) << exp1;

  success = mul_size(alot, alsoalot, &lots_n_lots);
    
  if(!success){
    return;
  }
    
  fprintf(stderr, "keys         = %zu\n", lots_n_lots);

    
  menagery = calloc(alot, sizeof(void *));
  if(menagery != NULL){
    
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "+");
      }
      
      void* zoo = calloc(alsoalot, sizeof(char));
      if(zoo != NULL){
	
	for(index = 0; index < alsoalot; index++){
	 found = linhash_insert(&numerouno, zoo + index, zoo + index);
	 if(!found){
	    fprintf(stderr, "linhash_insert FAILED: zindex = %zu index = %zu\n", zindex, index);
	    break;
	  }
	 assert(found);

	}
      }
      
      menagery[zindex] = zoo;
      
    }
  }
  
  fprintf(stderr, "\n");
  
  dump_linhash(stderr, &numerouno, false);

  if(menagery != NULL){
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "-");
      }
      
      void* zoo = menagery[zindex];
      if(zoo != NULL){
      
	for(index = 0; index < alsoalot; index++){
	  
	  found = linhash_delete(&numerouno, zoo + index);
	  if(!found){
	    fprintf(stderr, "linhash_delete FAILED: zindex = %zu index = %zu\n", zindex, index);
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
  
  dump_linhash(stderr, &numerouno, false);
  
  delete_linhash(&numerouno);
  
  free(menagery);
    
}


/*

PASCALI:

Using pool_memcxt
exp0 = 16 exp1 = 12
bincount_max = 4294967295
keys         = 268435456
real	14m10.821s
user	13m24.247s
sys	0m45.727s

Using sys_memcxt
exp0 = 16 exp1 = 12
bincount_max = 4294967295
keys         = 268435456
real	7m1.833s
user	6m44.852s
sys	0m16.680s


Using pool_memcxt
exp0 = 16 exp1 = 13
bincount_max = 4294967295
keys         = 536870912
real	28m4.328s
user	27m5.293s
sys	0m58.290s


Using sys_memcxt
exp0 = 16 exp1 = 13
bincount_max = 4294967295
keys         = 536870912
real	16m9.654s
user	15m19.912s
sys	0m49.196s



 */
