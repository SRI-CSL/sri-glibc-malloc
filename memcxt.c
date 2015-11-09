#include "memcxt.h"


#include <stdlib.h>
#include <string.h>


static void *default_malloc(memtype_t type, size_t size){
  return malloc(size);
}

static void *default_calloc(memtype_t type, size_t count, size_t size){
  return calloc(count, size);
}

static void default_free(memtype_t type, void *ptr){
  free(ptr);
}


memcxt_t default_memcxt = { default_malloc,  default_calloc, default_free };


memcxt_p sys_memcxt = &default_memcxt;

/*
 * Add two size_t values, checking for overflow
 */
size_t add_size(size_t s1, size_t s2){
  size_t result;
  result = s1 + s2;
  /* We are assuming size_t is an unsigned type here... */
  if (result < s1 || result < s2){
    //iam
    abort();
  }
  return result;
}


/*
 * Multiply two size_t values, checking for overflow
 */
size_t mul_size(size_t s1, size_t s2){
  size_t result;
  if (s1 == 0 || s2 == 0){
    return 0;
  }
  result = s1 * s2;
  /* We are assuming size_t is an unsigned type here... */
  if (result / s2 != s1){
    //iam
    abort();
  }
  return result;
}
