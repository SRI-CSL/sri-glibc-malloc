#include "memcxt.h"


#include <stdlib.h>
#include <string.h>


static void *default_allocate(memtype_t type, size_t size){
  void* mem;
  mem = malloc(size);
  memset(mem, 0, size);
  return mem;
}


static void default_release(memtype_t type, void *ptr, size_t ptr_sz){
  free(ptr);
}

void init_sys_memcxt(memcxt_t* smem){
  if(smem != NULL){
    smem->allocate =  default_allocate;
    smem->release = default_release;
  }
}



/* Add two size_t values, checking for overflow */
bool add_size(size_t s1, size_t s2, size_t* sum){
  size_t result;
  
  assert(sum != NULL);

  result = s1 + s2;
  if (result < s1 || result < s2){
    return false;
  }

  *sum = result;
  return true;

}


/* Multiply two size_t values, checking for overflow */
bool mul_size(size_t s1, size_t s2, size_t* prod){
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




