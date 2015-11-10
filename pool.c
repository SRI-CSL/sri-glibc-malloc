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
  size_t dsize;
  void *directory;
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} pool_t;




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

  /* beef this up later  */

  protection = PROT_READ|PROT_WRITE|MAP_ANON;
  
  flags = MAP_PRIVATE;
  
  memory = mmap(0, size, protection, flags, -1, 0);

  assert(memory != MAP_FAILED);

  return memory;
}

static void pool_munmap(void* memory, size_t size){
  int retcode;

  retcode = munmap(memory, size);

  assert(retcode != -1);
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

static bucket_t* alloc_bucket(pool_t* pool){
  bucket_t *buckp;
  bucket_pool_t* bpool_current;
  bucket_pool_t* bpool_previous;
  size_t scale;
  size_t index;
  
  bpool_current = pool->buckets;
  bpool_previous = NULL;
  buckp = NULL;

  while(bpool_current != NULL){

    if(bpool_current->free_count > 0){

      scale = 0;
      index = 0;

      /* let go through the blocks looking for a free bucket */
      while(scale < bp_scale){

	if(bpool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  while(index < 64){

	
	    if((bpool_current->bitmasks[scale] & (1 << index)) == 0){
	      /* ok we can use this one */

	      buckp = &bpool_current->pool[(scale * 64) + index];
	      
	      bpool_current->bitmasks[scale] |=  (1 << index);
	      bpool_current->free_count -= 1;

	      break;

	    } else {
	      index += 1;
	    }
      

	  }
	  
	  assert(buckp != NULL);
	  break;
	  
	} else {

	  /* move on to the next block */
	  scale += 1;
	  
	}

      }
	
    } else {
      bpool_previous = bpool_current;
      bpool_current = bpool_current->next_bucket_pool;
    }
  }

  if(buckp == NULL){
    /* need to allocate another bpool */

    assert(bpool_current  == NULL);
    assert(bpool_previous != NULL);


    /* TBD */

  } 

  return buckp;

}

static bool free_bucket(pool_t* pool, bucket_t* buckp){
  bool seen;
  bucket_pool_t* bpool;

  size_t index;
  size_t pmask_index;
  size_t mask;
  uint64_t pmask;

  
  assert(pool != NULL);
  assert(buckp != NULL);
  
  bpool = pool->buckets;
  seen = false;

  
  while ( bpool != NULL ){

    if( (bpool->pool <= buckp) && (buckp < bpool->pool + bp_length) ){

      index = buckp - bpool->pool;
      pmask_index = index / 64;
      pmask = bpool->bitmasks[pmask_index];
      mask = 1 << (index / bp_scale);

      assert(mask & pmask);

      bpool->bitmasks[pmask_index] = ~mask & pmask;
      bpool->free_count += 1;

      assert((bpool->free_count > 0) && (bpool->free_count <= bp_length));

    } else {

      bpool = bpool->next_bucket_pool; 

    } 
  }
  
  return seen;
  
}

//just one for now
static bool the_pool_is_ok = false;
static pool_t the_pool;

static void check_pool(void){
  if(!the_pool_is_ok){
    init_pool(&the_pool);
    the_pool_is_ok = true;
  }
}

static void *pool_allocate(memtype_t type, size_t size);

static void pool_release(memtype_t type, void *ptr, size_t size);

memcxt_t _pool_memcxt = { pool_allocate, pool_release };

memcxt_p pool_memcxt = &_pool_memcxt;



static void *pool_allocate(memtype_t type, size_t size){
  check_pool();
  return malloc(size);
}


static void pool_release(memtype_t type, void *ptr, size_t size){
  check_pool();
  switch(type){
  case DIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    pool_munmap(ptr, size);

    break;
  }
  case SEGMENT: break;
  case BUCKET: {
    free_bucket(&the_pool, ptr);
    break;
  }
  default: assert(false);
  }
  
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

