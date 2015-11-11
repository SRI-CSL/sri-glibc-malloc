#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stddef.h>

#include "types.h"

/*
 *  The API of our metadata pool allocator.
 *
 *
 */


typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;


// BD make sure it is well understood that allocate can fail.
// and make use of this in linhash.c etc.

struct memcxt_s {
  void *(*allocate)(memtype_t, size_t);
  void (*release)(memtype_t, void*, size_t);
};

/* memcxt_t is defined in types.h to be a memcxt_s */
extern memcxt_t* sys_memcxt;

extern size_t add_size(size_t s1, size_t s2);

extern size_t mul_size(size_t s1, size_t s2);



#endif
