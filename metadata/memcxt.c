#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <inttypes.h>

#include "memcxt.h"
#include "utils.h"

#ifndef NDEBUG
#warning "sanity checking in memcxt is on; FIX: "assert" here is determined by NDEBUG"
#endif

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


static void* new_directory(memcxt_t* memcxt, void* olddir, size_t size);

static segment_pool_t* new_segments(void);

static void* new_buckets(void);

static bucket_t* alloc_bucket(memcxt_t* memcxt);

static bool free_bucket(memcxt_t* memcxt, bucket_t* buckp);

static segment_t* alloc_segment(memcxt_t* memcxt);

static bool free_segment(memcxt_t* memcxt, segment_t* segp);

static void* pool_mmap(void* oldaddr, size_t size);

static bool pool_munmap(void* memory, size_t size);

bool init_memcxt(memcxt_t* memcxt){
  assert(memcxt != NULL);
  if(memcxt == NULL){
    return false;
  }
  memcxt->segments = new_segments();
  memcxt->buckets = new_buckets();

  memcxt->bcache = NULL;
  memcxt->bcache_count = 0;

  return memcxt->segments != NULL && memcxt->buckets != NULL;
}


void delete_memcxt(memcxt_t* memcxt){
  segment_pool_t* segments;
  segment_pool_t* currseg;
  bucket_pool_t* buckets;
  bucket_pool_t* currbuck;

  segments = memcxt->segments;
  memcxt->segments = NULL;
  if(segments != NULL){
    while(segments != NULL){
      currseg = segments;
      segments = segments->next_segment_pool;
      pool_munmap(currseg, sizeof(segment_pool_t));
    }
  }

  buckets = memcxt->buckets;
  memcxt->buckets = NULL;
  if(buckets != NULL){
    while(buckets != NULL){
      currbuck = buckets;
      buckets = buckets->next_bucket_pool;
      pool_munmap(currbuck, sizeof(bucket_pool_t));
    }
  }

}

void* memcxt_allocate(memcxt_t* memcxt, memtype_t type, void* oldptr, size_t sz){
  void *memory;

  memory = NULL;
  assert(memcxt != NULL);

  if(memcxt != NULL){
    
    switch(type){
    case DIRECTORY: {
      memory = new_directory(memcxt, oldptr, sz);
      break;
    }
    case SEGMENT: {
      assert(oldptr == NULL);
      memory = alloc_segment(memcxt);
      break;
    }
    case BUCKET: {
      assert(oldptr == NULL);
      memory = alloc_bucket(memcxt);
      break;
    }
    default: assert(false);
      memory = NULL;
    }
  }

  return memory;
  
}

void memcxt_release(memcxt_t* memcxt, memtype_t type,  void* ptr, size_t sz){
  assert(memcxt != NULL);

  if(memcxt != NULL){
    switch(type){
    case DIRECTORY: {
      pool_munmap(ptr, sz);
      break;
    }
    case SEGMENT: {
      free_segment(memcxt, ptr);
      break;
    }
    case BUCKET: {
      free_bucket(memcxt, ptr);
      break;
    }
    default: assert(false);
    }
  }
}

void dump_memcxt(FILE* fp, memcxt_t* memcxt){
  float bp;
  float sp;

  bp = sizeof(bucket_pool_t);
  sp = sizeof(segment_pool_t);
  bp /= 4096;
  sp /= 4096;
  fprintf(fp, "sizeof(bucket_pool_t) =  %zu\n", sizeof(bucket_pool_t));
  fprintf(fp, "pages: %f\n", bp);
  fprintf(fp, "sizeof(segment_pool_t) =  %zu\n", sizeof(segment_pool_t));
  fprintf(fp, "pages: %f\n", sp);
}



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

  for(bindex = 0; bindex < BP_LENGTH; bindex++){
    bp->pool[bindex].bucket_pool_ptr = bp;
  }
  assert(sane_bucket_pool(bp));
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


static void* new_directory(memcxt_t* memcxt, void* oldptr, size_t size){
  return pool_mmap(oldptr, size);
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
    for(bit = 0; bit < BITS_IN_MASK; bit++){
      if((mask & (((uint64_t)1) << bit)) == 0){
	bit_free_count++;
      }
    }
  }

  if(free_count != bit_free_count){
    fprintf(stderr, "sane_bucket_pool: free_count = %zu bit_free_count = %zu\n", free_count, bit_free_count);
    for(scale = 0; scale < BP_SCALE; scale++){
      fprintf(stderr, "\tbpool_current->bitmasks[%zu] = %" PRIu64 "\n", scale, bpool->bitmasks[scale]);
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
  assert((0 <= index) && (index < BITS_IN_MASK));
  return (mask & (((uint64_t)1) << index))  ? true : false;
}
#endif

/* sets the bit specified by the index in the mask */
static inline uint64_t set_bit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < BITS_IN_MASK);
  return mask | (((uint64_t)1) << index);
}

/* clear the bit */
static inline uint64_t clear_bit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < BITS_IN_MASK);
  return mask & ~(((uint64_t)1) << index); 
}

//static size_t yes = 0;
//static size_t no = 0;

static bucket_t* alloc_bucket(memcxt_t* memcxt){
  bucket_t *buckp;
  bucket_pool_t* bpool_current;
  size_t scale;
  size_t index;

  buckp = NULL;

  
  /* if the cache is not empty; get one from there */
  if(memcxt->bcache_count > 0){
    memcxt->bcache_count--;
    buckp = memcxt->bcache;
    memcxt->bcache = (bucket_t *)(buckp->chunk);
    buckp->chunk = NULL;
    //yes++;
    //fprintf(stderr, "yes = %zu  no = %zu; cache_length = %u\n", yes, no, memcxt->bcache_count);
    return buckp;
  } else {
    //no++;
  }
  
  
  for (bpool_current = memcxt->buckets; bpool_current != NULL; bpool_current = bpool_current->next_bucket_pool) {

    assert(sane_bucket_pool(bpool_current));

    if(bpool_current->free_count > 0){

      /* lets go through the blocks looking for a free bucket */
      for (scale = 0; scale < BP_SCALE; scale++){
	
	if(bpool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_bit(bpool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < BITS_IN_MASK));
	  buckp = &bpool_current->pool[(scale * BITS_IN_MASK) + index];
	  bpool_current->bitmasks[scale] = set_bit(bpool_current->bitmasks[scale], index);
	  bpool_current->free_count --;
	  assert(sane_bucket_pool(bpool_current));

	  assert(buckp != NULL);
	  
	  return buckp;
	}
      }
    }
  }

  assert(buckp == NULL);
  assert(bpool_current  == NULL);
  
  /* need to allocate another bpool */
  bpool_current = new_buckets();
  if (bpool_current != NULL) {
    /* put the new bucket up front */
    bpool_current->next_bucket_pool = memcxt->buckets;
    memcxt->buckets = bpool_current;
    
    /* return the first bucket of the new pool */
    buckp = &bpool_current->pool[0];
    bpool_current->free_count --;
    bpool_current->bitmasks[0] = 1; // low-order bit is set
  
  assert(sane_bucket_pool(bpool_current));
    assert(buckp != NULL);
  }

  return buckp;
}

static bool free_bucket(memcxt_t* memcxt, bucket_t* buckp){
  bucket_pool_t* bpool;

  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(memcxt != NULL);
  assert(buckp != NULL);

  /* add it to the cache if there is room */
  if(memcxt->bcache_count <  MAX_CACHE_LENGTH){
    buckp->chunk = (void *)(memcxt->bcache);
    memcxt->bcache = buckp;
    memcxt->bcache_count++;
    //for efficiency cached elements are left in use
    //so they can immediately be thrown back into the action.
    return true;
    
  }

  /* get the bucket pool that we belong to */
  bpool = buckp->bucket_pool_ptr;

  /* sanity check */
  assert((bpool->pool <= buckp) && (buckp < bpool->pool + BP_LENGTH));
  assert(sane_bucket_pool(bpool));

  index = buckp - bpool->pool;

  pmask_index = index / BITS_IN_MASK;
  pmask_bit = index % BITS_IN_MASK;

  assert(get_bit(bpool->bitmasks[pmask_index], pmask_bit)); 
	 
  bpool->bitmasks[pmask_index] = clear_bit(bpool->bitmasks[pmask_index], pmask_bit); 

  bpool->free_count ++;
  
  /* sanity check */
  assert((bpool->free_count > 0) && (bpool->free_count <= BP_LENGTH));
  assert(sane_bucket_pool(bpool));
  
  return true;
}


static segment_t* alloc_segment(memcxt_t* memcxt){
  segment_t *segp;
  segment_pool_t* spool_current;
  size_t scale;
  size_t index;
  
  segp = NULL;

  for (spool_current = memcxt->segments; spool_current != NULL; spool_current = spool_current->next_segment_pool) {

    if(spool_current->free_count > 0){

      /* lets go through the blocks looking for a free segment */
      for(scale = 0; scale < SP_SCALE; scale++){

	if(spool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_bit(spool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < BITS_IN_MASK));

	  /* ok we can use this one */
	  segp = &spool_current->pool[(scale * BITS_IN_MASK) + index];
	  spool_current->bitmasks[scale] = set_bit(spool_current->bitmasks[scale], index);
	  spool_current->free_count --;

	  assert(segp != NULL);
	  
	  return segp;
	  
	}
      }
    }
  }

  assert(segp == NULL);
  assert(spool_current  == NULL);

    
  spool_current = new_segments();
  
  if(spool_current != NULL){
    
    /* put the new segment up front */
    spool_current->next_segment_pool = memcxt->segments;
    memcxt->segments = spool_current;

    /* return the first segment in the new pool */
    segp = &spool_current->pool[0];
    spool_current->free_count --;
    spool_current->bitmasks[0] = 1;

  } 

  return segp;
}

static bool free_segment(memcxt_t* memcxt, segment_t* segp){
  segment_pool_t* spool;

  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(memcxt != NULL);
  assert(segp != NULL);
  
  /* get the segments pool that we belong to */
  spool = segp->segment_pool_ptr;

  /* sanity check */
  assert((spool->pool <= segp) && (segp < spool->pool + SP_LENGTH));

  index = segp - spool->pool;

  pmask_index = index / BITS_IN_MASK;
  pmask_bit = index % BITS_IN_MASK;

  assert(get_bit(spool->bitmasks[pmask_index], pmask_bit)); 

  spool->bitmasks[pmask_index] = clear_bit(spool->bitmasks[pmask_index], pmask_bit); 
  spool->free_count ++ ;

  assert((spool->free_count > 0) && (spool->free_count <= SP_LENGTH));
  
  return true;
}









