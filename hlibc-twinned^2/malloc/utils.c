#include "utils.h"

#include <stdlib.h>
#include <stdbool.h>

/* for sanity checking */
#ifndef NDEBUG
static bool is_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}
#endif


/* 
 * BD's Jenkins's lookup3 code 
 */

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define final(a,b,c)      \
{                         \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/*
 * BD's: Hash code for a 64bit integer
 */
uint32_t jenkins_hash_uint64(uint64_t x) {
  uint32_t a, b, c;

  a = (uint32_t) x; // low order bits
  b = (uint32_t) (x >> 32); // high order bits
  c = 0xdeadbeef;
  final(a, b, c);

  return c;
}


/*
 * BD's: Hash code for an arbitrary pointer p
 */
uint32_t jenkins_hash_ptr(const void *p) {
  return jenkins_hash_uint64((uint64_t) ((size_t) p));
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


