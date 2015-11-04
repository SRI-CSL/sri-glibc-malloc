#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stddef.h>

typedef struct memcxt_s {
  void *(*malloc)(size_t);
  void *(*calloc)(size_t, size_t);
  void (*free);
} memcxt_t;

typedef memcxt_t* memcxt_p;


extern memcxt_p sys_memcxt;


extern size_t add_size(size_t s1, size_t s2);

extern size_t mul_size(size_t s1, size_t s2);





#endif
