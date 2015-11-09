#include "pool.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>


const size_t bp_scale = 8;
const size_t bp_length = bp_scale * 64;  /* one thing for every bit in the bitmask */

const size_t sp_scale = 8;
const size_t sp_length = sp_scale * 64;  /* one thing for every bit in the bitmask */


typedef struct segment_s {
  void* segment[SEGMENT_LENGTH];
} segment_t;

/* if we keep our pools the same size, scale, we can use a header:
typedef struct poolhdr_s {
  size_t free_count;
  void* next_pool;
  uint64_t bitmasks[scale];    
} poolhdr_t;
*/

typedef struct bucket_pool_s {
  size_t free_count;
  void* next_bucket_pool;
  uint64_t bitmasks[bp_scale];    /* zero means: free; one means: in use */
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
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} pool_t;

//just one for now
static pool_t the_pool;

/* for now we assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_bucket_pool(bucket_pool_t* bp){
  bp->free_count = bp_length;
}

/* for now we assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_segment_pool(segment_pool_t* sp){
  sp->free_count = sp_length;
}

static void* pool_mmap(size_t size){
  void* memory;
  int flags;
  int protection;

  /* beef this up later http://man7.org/tlpi/code/online/dist/mmap/anon_mmap.c.html  */

  protection = PROT_READ|PROT_WRITE|MAP_ANON;
  
  flags = MAP_PRIVATE;
  
  /* Use MAP_NORESERVE if available (Solaris, HP-UX; most other
   * systems use defered allocation anyway.
   */
#ifdef MAP_NORESERVE
  flags |= MAP_NORESERVE;
#endif

  memory = mmap(0, size, protection, flags, -1, 0);

  assert(memory != MAP_FAILED);

  return memory;
}


static void* new_directory(size_t size){
  return pool_mmap(size);
}

static segment_pool_t* new_segments(void){
  segment_pool_t* sptr;
  sptr = pool_mmap(sizeof(segment_pool_t));
  init_segment_pool(sptr);
  return sptr;
}

static void* new_buckets(void){
  bucket_pool_t* bptr;
  bptr = pool_mmap(sizeof(bucket_pool_t));
  init_bucket_pool(bptr);
  return bptr;
}


static void init_pool(pool_t* pool){
  pool->directory = new_directory(DIRECTORY_LENGTH * sizeof(void*));
  pool->segments = new_segments();
  pool->buckets = new_buckets();
}

static void bucket_t* alloc_bucket(pool_t* pool){

}

static void void free_bucket(pool_t* pool, bucket_t* buckp){

}

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

