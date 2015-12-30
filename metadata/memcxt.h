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

#define BITS_IN_MASK  64

#define BP_SCALE  1024
/* one thing for every bit in the bitmask */
#define BP_LENGTH  BP_SCALE * BITS_IN_MASK  

#define SP_SCALE  8
/* one thing for every bit in the bitmask */
#define SP_LENGTH SP_SCALE * BITS_IN_MASK  

struct bucket_pool_s {
  bucket_t pool[BP_LENGTH];       /* the pool of buckets; one for each bit in the bitmask array */
  uint64_t bitmasks[BP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use */
  size_t free_count;              /* the current count of free buckets in this pool             */
  void* next_bucket_pool;         /* the next bucket pool to look if this one is full           */
};

struct segment_pool_s {
  segment_t pool[SP_LENGTH];      /* the pool of segments; one for each bit in the bitmask array */
  uint64_t bitmasks[SP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use  */
  size_t free_count;              /* the current count of free segments in this pool             */
  void* next_segment_pool;        /* the next segment pool to look if this one is full           */
};


typedef struct memcxt_s {
  segment_pool_t* segments;
  bucket_pool_t* buckets;
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
