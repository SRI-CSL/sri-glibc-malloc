#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

/*
 *  The API of our pool allocator for the metadata.
 *
 *
 */

typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;

typedef struct bucket_pool_s bucket_pool_t;

typedef struct segment_pool_s segment_pool_t;

typedef struct memcxt_s memcxt_t;

typedef struct segment_s segment_t;

#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024

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

struct segment_s {
  bucket_t* segment[SEGMENT_LENGTH];
  segment_pool_t *segment_pool_ptr;  
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

#endif
