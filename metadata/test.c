#include "metadata.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "pool.h"
#include "memcxt.h"

metadata_t numerouno;


static void test_0(memcxt_t* memcxt);
static void test_1(memcxt_t* memcxt);
static void test_2(memcxt_t* memcxt);
static void test_3(memcxt_t* memcxt);

#define K   1024
#define K2  K << 1
#define K4  K2 << 1



int main(int argc, char** argv){
  memcxt_t memcxt;
  int tests[] = { 0, 0, 1, 0};

  init_memcxt(&memcxt);

  fprintf(stderr, "Using %s\n",  (argc > 1) ? "sys_memcxt" : "pool_memcxt");

  if(tests[0]){ test_0(&memcxt); }
  
  if(tests[1]){ test_1(&memcxt); }

  if(tests[2]){ test_2(&memcxt); }

  if(tests[3]){ test_3(&memcxt); }


  delete_memcxt(&memcxt);
  
  return 0;
}


void test_0(memcxt_t* memcxt){
  int chunk;
  void* look;
  bool success;
  
  init_metadata(&numerouno, memcxt);

  fprintf(stderr, "inserting chunk = %p\n", &chunk);
  
  metadata_insert_chunk(&numerouno, &chunk);

  look = metadata_lookup(&numerouno, &chunk);

  fprintf(stderr, "chunk = %p  look = %p\n", &chunk, look);

  dump_metadata(stderr, &numerouno, true);

  success = metadata_delete(&numerouno, &chunk);

  fprintf(stderr, "chunk = %p  success = %d\n", &chunk, success);

  dump_metadata(stderr, &numerouno, true);

  success = metadata_delete(&numerouno, &chunk);

  fprintf(stderr, "chunk = %p success = %d\n", &chunk, success);

  dump_metadata(stderr, &numerouno, true);

  delete_metadata(&numerouno);

  dump_memcxt(stderr, memcxt);
 
}



void test_1(memcxt_t* memcxt){
  bool found;
  size_t index;
  
  void* zoo = calloc(K2, sizeof(char));
  
  init_metadata(&numerouno, memcxt);

  for(index = 0; index < K2; index++){
    metadata_insert_chunk(&numerouno, zoo + index);
  }

  dump_metadata(stderr, &numerouno, true);
    
  for(index = 0; index < K2; index++){
    found = metadata_delete(&numerouno, zoo + index);
    if(!found){
      fprintf(stderr, "index = %" PRIuPTR "\n", index);
    }
    assert(found);
  }
  
  dump_metadata(stderr, &numerouno, true);

  delete_metadata(&numerouno);

  free(zoo);
  
}


void test_2(memcxt_t* memcxt){
  bool found;
  size_t index;
  size_t zindex;

  void ** menagery;
  

  init_metadata(&numerouno, memcxt);

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
      
      metadata_insert_chunk(&numerouno, zoo + index);
    }

    menagery[zindex] = zoo;

  }

  dump_metadata(stderr, &numerouno, false);

  for(zindex = 0; zindex < K4; zindex++){

    if(0 && (zindex % K == 0)){
      fprintf(stderr, "> zindex = %" PRIuPTR "\n", zindex);
    }

    void* zoo = menagery[zindex];
  
    for(index = 0; index < K4; index++){

      if(0 && (zindex % K == 0) && (index % K == 0)){
	fprintf(stderr, "zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
      }

      found = metadata_delete(&numerouno, zoo + index);
      if(!found){
	fprintf(stderr, "zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
      }
      assert(found);
    }

    
    menagery[zindex] = NULL;
    
    
    free(zoo);

    if(!found){ break; }

  }
  
  dump_metadata(stderr, &numerouno, false);

  delete_metadata(&numerouno);

  free(menagery);

  dump_memcxt(stderr, memcxt);

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

  init_metadata(&numerouno, memcxt);

  fprintf(stderr, "exp0 = %d exp1 = %d\n", exp0, exp1);

  fprintf(stderr, "bincount_max = %" PRIuPTR "\n", numerouno.cfg.bincount_max);

  alot = ((uint64_t)1) << exp0;

  alsoalot = ((uint64_t)1) << exp1;

  success = mul_size(alot, alsoalot, &lots_n_lots);
    
  if(!success){
    return;
  }
    
  fprintf(stderr, "chunks         = %" PRIuPTR "\n", lots_n_lots);

    
  menagery = calloc(alot, sizeof(void *));
  if(menagery != NULL){
    
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "+");
      }
      
      void* zoo = calloc(alsoalot, sizeof(char));
      if(zoo != NULL){
	
	for(index = 0; index < alsoalot; index++){
	 found = metadata_insert_chunk(&numerouno, zoo + index);
	 if(!found){
	    fprintf(stderr, "metadata_insert FAILED: zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
	    break;
	  }
	 assert(found);

	}
      }
      
      menagery[zindex] = zoo;
      
    }
  }
  
  fprintf(stderr, "\n");
  
  dump_metadata(stderr, &numerouno, false);

  if(menagery != NULL){
    for(zindex = 0; zindex < alot; zindex++){
      
      if((zindex % K4 == 0)){
	fprintf(stderr, "-");
      }
      
      void* zoo = menagery[zindex];
      if(zoo != NULL){
      
	for(index = 0; index < alsoalot; index++){
	  
	  found = metadata_delete(&numerouno, zoo + index);
	  if(!found){
	    fprintf(stderr, "metadata_delete FAILED: zindex = %" PRIuPTR " index = %" PRIuPTR "\n", zindex, index);
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
  
  dump_metadata(stderr, &numerouno, false);
  
  delete_metadata(&numerouno);
  
  free(menagery);
    
}


