#ifndef _HASHFNS_H
#define _HASHFNS_H

#include <stdint.h>


/*
 *  Bruno Dutertre's versions of Jenkin's hash functions.
 *
 */

extern uint32_t jenkins_hash_uint64(uint64_t x);

extern uint32_t jenkins_hash_ptr(const void *p);


#endif
