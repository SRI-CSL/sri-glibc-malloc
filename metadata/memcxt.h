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

#define CACHE_LENGTH 4

typedef struct memcxt_s {
  segment_pool_t* segments;
  bucket_pool_t* buckets;
  bucket_t* bcache[CACHE_LENGTH];
  uint32_t bcache_count;
} memcxt_t;


typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;

extern bool init_memcxt(memcxt_t* memcxt);

/* 
 * Attempts to allocate a block of memory of the appropriate type from the memcxt.
 * The size and oldptr are onlye used when the type is DIRECTORY. 
 * Since in the other two cases the size of the object required is fixed at 
 * compile time.
 * In the case of a directory we use the oldptr to see if we can realloc the
 * old directory to the newly desired size (which could be smaller).
 *
 * All these routines can return NULL.
 *
 */
extern void* memcxt_allocate(memcxt_t* memcxt, memtype_t type, void* oldptr, size_t size);

extern void memcxt_release(memcxt_t* memcxt, memtype_t,  void*, size_t);

extern void delete_memcxt(memcxt_t* memcxt);

extern void dump_memcxt(FILE* fp, memcxt_t* memcxt);

#endif
