#ifndef _HASHFNS_H
#define _HASHFNS_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


/*
 *  Bruno Dutertre's versions of Jenkin's hash functions.
 *
 */

extern uint32_t jenkins_hash_uint64(uint64_t x);

extern uint32_t jenkins_hash_ptr(const void *p);


/* routines for size_t multiplication and size_t addition that detect overflow */

extern bool add_size(size_t s1, size_t s2, size_t* sum);

extern bool mul_size(size_t s1, size_t s2, size_t* prod);



#endif
