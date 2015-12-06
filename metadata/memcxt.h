#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

//#include "types.h"

/*
 *  The API of our pool allocator for the metadata.
 *
 *
 */

typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;

typedef struct bucket_pool_s bucket_pool_t;

typedef struct segment_pool_s segment_pool_t;

typedef struct memcxt_s memcxt_t;


#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

typedef void * mchunkptr;

typedef struct chunkinfo {
  INTERNAL_SIZE_T   prev_size;     /* Size of previous in bytes          */
  INTERNAL_SIZE_T   size;          /* Size in bytes, including overhead. */
  INTERNAL_SIZE_T   req;           /* Original request size, for guard.  */
  struct chunkinfo*  fd;	   /* double links -- used only if free. */
  struct chunkinfo*  bk;           /* double links -- used only if free. */
  struct chunkinfo*  next_bucket;  /* next bucket in the bin             */
  mchunkptr chunk;                  
  bucket_pool_t* bucket_pool_ptr;  //BD's optimization #1.
} bucket_t;

typedef bucket_t* chunkinfoptr;

#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024

typedef struct segment_s {
  bucket_t* segment[SEGMENT_LENGTH];
  segment_pool_t *segment_pool_ptr;  
} segment_t;


#define BP_SCALE  1024
/* one thing for every bit in the bitmask */
#define BP_LENGTH  BP_SCALE * 64  

#define SP_SCALE  8
/* one thing for every bit in the bitmask */
#define SP_LENGTH SP_SCALE * 64  

/* bucket_pool_t is defined in types.h */
struct bucket_pool_s {
  bucket_t pool[BP_LENGTH];       /* the pool of buckets; one for each bit in the bitmask array */
  uint64_t bitmasks[BP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use */
  size_t free_count;              /* the current count of free buckets in this pool             */
  void* next_bucket_pool;         /* the next bucket pool to look if this one is full           */
};

/* segment_pool_t is defined in types.h */
struct segment_pool_s {
  segment_t pool[SP_LENGTH];      /* the pool of segments; one for each bit in the bitmask array */
  uint64_t bitmasks[SP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use  */
  size_t free_count;              /* the current count of free segments in this pool             */
  void* next_segment_pool;        /* the next segment pool to look if this one is full           */
};


//DD: is there a one-one or a many-to-one relationship b/w linhash tables and pools?
typedef struct pool_s {
  void *directory;              /* not sure if this is needed/desired */
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} pool_t;


struct memcxt_s {
  pool_t pool;
};



extern bool init_memcxt(memcxt_t* memcxt);

extern void* memcxt_allocate(memcxt_t* memcxt, memtype_t, size_t);

extern void memcxt_release(memcxt_t* memcxt, memtype_t,  void*, size_t);

extern void delete_memcxt(memcxt_t* memcxt);

extern void dump_memcxt(FILE* fp, memcxt_t* memcxt);

/* routines for size_t multiplication and size_t addition that detect overflow */

extern bool add_size(size_t s1, size_t s2, size_t* sum);

extern bool mul_size(size_t s1, size_t s2, size_t* prod);



#endif
