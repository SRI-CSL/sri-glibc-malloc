#include "pool.h"

#include <stdlib.h>


const size_t bp_scale = 8;
const size_t bp_length = bp_scale * 64;  /* one thing for every bit in the bitmask */

const size_t sp_scale = 8;
const size_t sp_length = sp_scale * 64;  /* one thing for every bit in the bitmask */


typedef struct segment_s {
  void* segment[SEGMENT_SIZE];
} segment_t;


typedef struct bucket_pool_s {
  size_t free_count;
  void* next_bucket_pool;
  uint64_t bitmasks[bp_scale];
  bucket_t pool[bp_length];
}  bucket_pool_t;

typedef struct segment_pool_s {
  size_t free_count;
  void* next_segment_pool;
  uint64_t bitmasks[sp_scale];
  segment_t pool[sp_length];
}  segment_pool_t;



typedef struct pool_s {
  void *directory;
  segment_pool_t segments;
  bucket_pool_t buckets;
} pool_t;


static void *pool_malloc(memtype_t type, size_t size);

static void *pool_calloc(memtype_t type, size_t count, size_t size);

static void pool_free(memtype_t type, void *ptr);

memcxt_t _pool_memcxt = { pool_malloc,  pool_calloc, pool_free };

memcxt_p pool_memcxt = &_pool_memcxt;


static void *pool_malloc(memtype_t type, size_t size){
  return malloc(size);
}

static void *pool_calloc(memtype_t type, size_t count, size_t size){
  return calloc(count, size);
}

static void pool_free(memtype_t type, void *ptr){
  free(ptr);
}


void dump_pool(FILE* fp){
  float bp = sizeof(bucket_pool_t);
  float sp = sizeof(segment_pool_t);
  bp /= 4096;
  sp /= 4096;
  fprintf(fp, "sizeof(bucket_pool_t) =  %zu\n", sizeof(bucket_pool_t));
  fprintf(fp, "pages: %f\n", bp);
  fprintf(fp, "sizeof(segment_pool_t) =  %zu\n", sizeof(segment_pool_t));
  fprintf(fp, "pages: %f\n", sp);

}
