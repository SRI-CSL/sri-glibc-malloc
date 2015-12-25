#ifndef _CHUNKINFO_H
#define _CHUNKINFO_H



#include <stdbool.h>
#include <stddef.h>

typedef struct bucket_pool_s bucket_pool_t;

typedef struct segment_pool_s segment_pool_t;

#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024

#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

/* based on the dlmalloc chunk not the glibc chunk */
typedef struct chunkinfo {
  INTERNAL_SIZE_T   size;          /* Size in bytes, including overhead. */
  INTERNAL_SIZE_T   prev_size;     /* Size of previous in bytes          */
  INTERNAL_SIZE_T   req;           /* Original request size, for guard.  */
  struct chunkinfo*  fd;	   /* double links -- used only if free. */
  struct chunkinfo*  bk;           /* double links -- used only if free. */
  struct chunkinfo*  next_bucket;  /* next bucket in the bin             */
  void * chunk;                    /* the actual client memory           */
  bucket_pool_t* bucket_pool_ptr;  /* BD's optimization #1.              */
} bucket_t;

typedef struct chunkinfo* chunkinfoptr;

typedef struct segment_s {
  bucket_t* segment[SEGMENT_LENGTH];
  segment_pool_t *segment_pool_ptr;  
} segment_t;

#endif
