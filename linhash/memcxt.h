#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stdbool.h>
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

extern void init_sys_memcxt(memcxt_t* smem);

extern bool add_size(size_t s1, size_t s2, size_t* sum);

extern bool mul_size(size_t s1, size_t s2, size_t* prod);



#endif
