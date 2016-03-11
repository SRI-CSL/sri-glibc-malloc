#include <stdbool.h>
#include <stddef.h>

typedef struct bucket_pool_s bucket_pool_t;

typedef struct segment_pool_s segment_pool_t;

#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024

#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

/* based on the glibc chunk not the dlmalloc chunk  */
typedef struct chunkinfo {
  INTERNAL_SIZE_T   prev_size;       /* Size of previous chunk (if free).  used in malloc.[ch]    */
  INTERNAL_SIZE_T   size;            /* Size in bytes, including overhead. used in malloc.[ch]    */
  struct chunkinfo* fd;              /* double links -- used only if free. used in malloc.[ch]    */
  struct chunkinfo* bk;
                                     /* Only used for large blocks.        used in malloc.[ch]    */
                                     /* pointer to next larger size.       used in malloc.[ch]    */
  struct chunkinfo* fd_nextsize;     /* double links -- used only if free. used in malloc.[ch]    */
  struct chunkinfo* bk_nextsize;
  void*             chunk;           /* the actual client memory           used in malloc.[ch]    */

  struct chunkinfo* next_bucket;     /* next bucket in the bin             used in metadata.[ch]  */
  bucket_pool_t*    bucket_pool_ptr; /* pointer to my bucket pool.     used in metadata.[ch]  */
} bucket_t;

typedef bucket_t* chunkinfoptr;

typedef struct segment_s {
  bucket_t* segment[SEGMENT_LENGTH];
  segment_pool_t *segment_pool_ptr;  
} segment_t;
