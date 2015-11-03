#include "memcxt.h"


#include <stdlib.h>

memcxt_t default_memcxt = { malloc, free };


memcxt_p sys_memcxt = &default_memcxt;


/*
 * Add two size_t values, checking for overflow
 */
size_t
add_size(size_t s1, size_t s2)
{
  size_t		result;
  
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
size_t
mul_size(size_t s1, size_t s2)
{
	size_t		result;

	if (s1 == 0 || s2 == 0)
		return 0;
	result = s1 * s2;
	/* We are assuming size_t is an unsigned type here... */
	if (result / s2 != s1){
	  
	  //iam
	  abort();
	}
	return result;
}
