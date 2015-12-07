#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stdint.h>
#include <stdio.h>

#include "chunkinfo.h"

/*
 *  The API of our pool allocator for the metadata.
 *
 *
 */



typedef struct memcxt_s {
  void* directory;
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} memcxt_t;


typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;

extern bool init_memcxt(memcxt_t* memcxt);

extern void* memcxt_allocate(memcxt_t* memcxt, memtype_t, size_t);

extern void memcxt_release(memcxt_t* memcxt, memtype_t,  void*, size_t);

extern void delete_memcxt(memcxt_t* memcxt);

extern void dump_memcxt(FILE* fp, memcxt_t* memcxt);

#endif
