#include "pool.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "linhash.h"
#include "memcxt.h"

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

#ifndef NDEBUG
static bool sane_bucket_pool(bucket_pool_t* bpool);
#endif

/* for now we do not assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_bucket_pool(bucket_pool_t* bp){
  size_t scale;
  size_t bindex;

  bp->free_count = BP_LENGTH;

  for(scale = 0; scale < BP_SCALE; scale++){
    bp->bitmasks[scale] = 0;
  }
  bp->next_bucket_pool = NULL;
  assert(sane_bucket_pool(bp));

  for(bindex = 0; bindex < BP_LENGTH; bindex++){
    bp->pool[bindex].bucket_pool_ptr = bp;
  }
}

/* for now we  do not assume assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_segment_pool(segment_pool_t* sp){
  size_t scale;
  size_t sindex;
  sp->free_count = SP_LENGTH;
  for(scale = 0; scale < SP_SCALE; scale++){
    sp->bitmasks[scale] = 0;
  }
  sp->next_segment_pool = NULL;

  for(sindex = 0; sindex < SP_LENGTH; sindex++){
    sp->pool[sindex].segment_pool_ptr = sp;
  }

}

static void* pool_mmap(void* oldaddr, size_t size){
  void* memory;
  int flags;
  int protection;

  /* beef this up later  */

  protection = PROT_READ | PROT_WRITE;
  flags = MAP_PRIVATE | MAP_ANON;

  /* try extending the current region */
  memory = mmap(oldaddr, size, protection, flags, -1, 0);

  /* if extending fails, then just try and map a new one */
  if((oldaddr != NULL) && (memory == MAP_FAILED)){
    memory = mmap(0, size, protection, flags, -1, 0);
  }
  
  if(memory == MAP_FAILED){
    memory = NULL;
  }

  return memory;
}

static bool pool_munmap(void* memory, size_t size){
  int rcode;

  rcode = munmap(memory, size);
  
  return rcode != -1;
}


static void* new_directory(pool_t* pool, size_t size){
  pool->directory = pool_mmap(pool->directory, size);
  return pool->directory;
}

static segment_pool_t* new_segments(void){
  segment_pool_t* sptr;
  sptr = pool_mmap(NULL, sizeof(segment_pool_t));
  if(sptr == NULL){
    return NULL;
  }
  init_segment_pool(sptr);
  return sptr;
}

static void* new_buckets(void){
  bucket_pool_t* bptr;
  bptr = pool_mmap(NULL, sizeof(bucket_pool_t));
  if(bptr == NULL){
    return NULL;
  }
  init_bucket_pool(bptr);
  return bptr;
}

#ifndef NDEBUG
#if 0
static bool sane_bucket_pool(bucket_pool_t* bpool){
  size_t free_count;
  size_t bit_free_count;
  size_t scale;
  size_t bindex;
  uint64_t bit;
  uint64_t mask;
  
  free_count = bpool->free_count;
  bit_free_count = 0;

  for(scale = 0; scale < BP_SCALE; scale++){
    mask = bpool->bitmasks[scale];
    for(bit = 0; bit < 64; bit++){
      if((mask & (((uint64_t)1) << bit)) == 0){
	bit_free_count++;
      }
    }
  }

  if(free_count != bit_free_count){
    fprintf(stderr, "sane_bucket_pool: free_count = %zu bit_free_count = %zu\n", free_count, bit_free_count);
    for(scale = 0; scale < BP_SCALE; scale++){
      fprintf(stderr, "\tbpool_current->bitmasks[%zu] = %llu\n", scale, bpool->bitmasks[scale]);
    }
    return false;
  }

  for(bindex = 0; bindex < BP_LENGTH; bindex++){
    if(bpool->pool[bindex].bucket_pool_ptr != bpool){
      fprintf(stderr, "sane_bucket_pool: bucket_pool_ptr = %p not correct:  bucket_pool_t* %p\n",
	      bpool->pool[bindex].bucket_pool_ptr, bpool);
    }
  }

  return true;
}
#else
static inline bool sane_bucket_pool(bucket_pool_t *bpool) {
  return true;
}
#endif
#endif


static void init_pool(pool_t* pool){
  pool->directory = new_directory(pool, DIRECTORY_LENGTH * sizeof(void*));
  pool->segments = new_segments();
  pool->buckets = new_buckets();
}


// courtesy of BD 
#ifdef __GNUC__

static inline uint32_t ctz64(uint64_t x) {
  assert(x != 0);
  return __builtin_ctzl(x);
}

#else

static inline uint32_t ctz64(uint64_t x) {
  uint64_t m;
  uint32_t i;

  assert(x != 0);
  m = 1;
  i = 0;
  while ((x & m) == 0) {
    i ++;
    m += m;
  }
  return i;
}

#endif

/* returns the index of the lowest order bit that is zero */
static uint32_t get_free_bit(uint64_t mask){
  uint32_t index;
  uint64_t flipped;
  assert(mask != UINT64_MAX);
  flipped = ~mask;
  index = ctz64(flipped);  
  return index;
}

#ifndef NDEBUG
static inline bool get_bit(uint64_t mask, uint32_t index){
  assert((0 <= index) && (index < 64));
  return (mask & (((uint64_t)1) << index))  ? true : false;
}
#endif

/* sets the bit specified by the index in the mask */
static inline uint64_t set_bit(uint64_t mask, uint32_t index, bool val){
  assert((0 <= index) && (index < 64));
  return mask | (((uint64_t)1) << index);
}

static bucket_t* alloc_bucket(pool_t* pool){
  bucket_t *buckp;
  bucket_pool_t* bpool_current;
  size_t scale;
  size_t index;
  
  bpool_current = pool->buckets;
  buckp = NULL;
  
  while(bpool_current != NULL){

    assert(sane_bucket_pool(bpool_current));

    if(bpool_current->free_count > 0){

      /* lets go through the blocks looking for a free bucket */
      for(scale = 0; scale < BP_SCALE; scale++)
	
	if(bpool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_bit(bpool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));
	  buckp = &bpool_current->pool[(scale * 64) + index];
	  bpool_current->bitmasks[scale] = set_bit(bpool_current->bitmasks[scale], index, true);
	  bpool_current->free_count -= 1;
	  assert(sane_bucket_pool(bpool_current));

	  assert(buckp != NULL);
	  break;
	  
	}

      if(buckp != NULL){
	break;
      }

    } else {
      bpool_current = bpool_current->next_bucket_pool;
    }
    
  }

  if(buckp == NULL){
    /* need to allocate another bpool */

    assert(bpool_current  == NULL);

    bpool_current = new_buckets();
    if(bpool_current == NULL){
      return NULL;
    }

    /* put the new bucket up front */
    bpool_current->next_bucket_pool = pool->buckets;
    pool->buckets = bpool_current;
    
    /* return the first bucket of the new pool */
    buckp = &bpool_current->pool[0];
    bpool_current->free_count -= 1;
    bpool_current->bitmasks[0] = 1;
  }
  
  assert(sane_bucket_pool(bpool_current));

  return buckp;
}

static bool free_bucket(pool_t* pool, bucket_t* buckp){
  bucket_pool_t* bpool;

  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(pool != NULL);
  assert(buckp != NULL);

  /* get the bucket pool that we belong to */
  bpool = buckp->bucket_pool_ptr;

  /* sanity check */
  assert((bpool->pool <= buckp) && (buckp < bpool->pool + BP_LENGTH));

  index = buckp - bpool->pool;

  pmask_index = index / 64;
  pmask_bit = index % 64;

  assert(get_bit(bpool->bitmasks[pmask_index], pmask_bit)); 
	 
  bpool->bitmasks[pmask_index] = set_bit(bpool->bitmasks[pmask_index], pmask_bit, false); 

  bpool->free_count += 1;
  
  assert((bpool->free_count > 0) && (bpool->free_count <= BP_LENGTH));
  
  return true;
}


static segment_t* alloc_segment(pool_t* pool){
  segment_t *segp;
  segment_pool_t* spool_current;
  size_t scale;
  size_t index;
  
  spool_current = pool->segments;
  segp = NULL;

  assert(spool_current != NULL);
  
  while(spool_current != NULL){

    if(spool_current->free_count > 0){

      /* lets go through the blocks looking for a free segment */
      for(scale = 0; scale < SP_SCALE; scale++){

	if(spool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_bit(spool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));

	  /* ok we can use this one */
	  segp = &spool_current->pool[(scale * 64) + index];
	  spool_current->bitmasks[scale] = set_bit(spool_current->bitmasks[scale], index, true);
	  spool_current->free_count -= 1;

	  assert(segp != NULL);
	  break;
	  
	}
      }
      
      if(segp != NULL){ break; }
      
    } else {
      spool_current = spool_current->next_segment_pool;
    }
  }

  if(segp == NULL){
    /* need to allocate another spool */

    assert(spool_current  == NULL);

    
    spool_current = new_segments();
    if(spool_current == NULL){
      return NULL;
    }

    /* put the new segment up front */
    spool_current->next_segment_pool = pool->segments;
    pool->segments = spool_current;

    /* return the first segment in the new pool */
    segp = &spool_current->pool[0];
    spool_current->free_count -= 1;
    spool_current->bitmasks[0] = 1;
  } 

  return segp;
}

static bool free_segment(pool_t* pool, segment_t* segp){
  segment_pool_t* spool;

  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(pool != NULL);
  assert(segp != NULL);
  
  /* get the segments pool that we belong to */
  spool = segp->segment_pool_ptr;

  /* sanity check */
  assert((spool->pool <= segp) && (segp < spool->pool + SP_LENGTH));

  index = segp - spool->pool;

  pmask_index = index / 64;
  pmask_bit = index % 64;

  assert(get_bit(spool->bitmasks[pmask_index], pmask_bit)); 

  spool->bitmasks[pmask_index] = set_bit(spool->bitmasks[pmask_index], pmask_bit, false); 
  spool->free_count += 1;

  assert((spool->free_count > 0) && (spool->free_count <= SP_LENGTH));
  
  return true;
}

/* just one for now */
static bool the_pool_is_ok = false;
static pool_t the_pool;

static void check_pool(void){
  if(!the_pool_is_ok){
    init_pool(&the_pool);
    the_pool_is_ok = true;
  }
}

static void *pool_allocate(pool_t* pool, memtype_t type, size_t size);

static void pool_release(pool_t* pool, memtype_t type, void *ptr, size_t size);



static void *_pool_allocate(memtype_t type, size_t size);

static void _pool_release(memtype_t type, void *ptr, size_t size);




memcxt_t _pool_memcxt = { _pool_allocate, _pool_release };

memcxt_t *pool_memcxt = &_pool_memcxt;


static void *_pool_allocate(memtype_t type, size_t size){
  return pool_allocate(&the_pool, type, size);
}

static void _pool_release(memtype_t type, void *ptr, size_t size){
  pool_release(&the_pool, type, ptr, size);
}



size_t pcount = 0;

static void *pool_allocate(pool_t* pool, memtype_t type, size_t size){
  void *memory;
  check_pool();
  switch(type){
  case DIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    // how many hash tables use this pool?
    memory = new_directory(pool, size);
    break;
  }
  case SEGMENT: {
    memory = alloc_segment(pool);
    break;
  }
  case BUCKET: {
    memory = alloc_bucket(pool);
    break;
  }
  default: assert(false);
    memory = NULL;
  }
  return memory;
}


static void pool_release(pool_t* pool, memtype_t type, void *ptr, size_t size){
  check_pool();
  switch(type){
  case DIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    pool_munmap(ptr, size);

    break;
  }
  case SEGMENT: {
    free_segment(pool, ptr);
    break;
  }
  case BUCKET: {
    free_bucket(pool, ptr);
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

