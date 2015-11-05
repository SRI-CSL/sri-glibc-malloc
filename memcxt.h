#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stddef.h>

/*
 *  The beginnings of our metadata pool allocator.
 *
 *
 */



typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;



typedef struct memcxt_s {
  void *(*malloc)(memtype_t, size_t);
  void *(*calloc)(memtype_t, size_t, size_t);
  void (*free)(memtype_t, void*);
} memcxt_t;

typedef memcxt_t* memcxt_p;


extern memcxt_p sys_memcxt;


extern size_t add_size(size_t s1, size_t s2);

extern size_t mul_size(size_t s1, size_t s2);





#endif
