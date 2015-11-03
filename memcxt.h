#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stddef.h>

typedef struct memcxt_s {
  void *(*alloc)(size_t);
  void (*free);
} memcxt_t;

typedef memcxt_t* memcxt_p;


extern memcxt_p sys_memcxt;


#define MAXIMUM_ALIGNOF 8

#define TYPEALIGN(ALIGNVAL,LEN) (((intptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((intptr_t) ((ALIGNVAL) - 1)))

#define MAXALIGN(LEN) TYPEALIGN(MAXIMUM_ALIGNOF, (LEN))


extern size_t add_size(size_t s1, size_t s2);

extern size_t mul_size(size_t s1, size_t s2);





#endif
