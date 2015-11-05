#include "linhash.h"

#include <assert.h>

/* static routines */
static bool is_power_of_two(uint32_t n);
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* returns the abstract offset/index of the bin that should contain p */
static uint32_t linhash_offset(linhash_t* htbl, const void *p);

static void linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);
static void linhash_expand_table(linhash_t* lhtbl, memcxt_t* memcxt);


/* stolen from BD's opus */
static uint32_t jenkins_hash_uint64(uint64_t x);
static uint32_t jenkins_hash_ptr(const void *p);


/* Fast MOD arithmetic, assuming that y is a power of 2 ! */
#define MOD(x,y)  ((x) & ((y)-1))

/* for sanity checking */
static bool is_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}


static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt){
  cfg->multithreaded          = false;
  cfg->segment_size           = 256;
  cfg->initial_directory_size = 256;
  cfg->min_load               = 2;   
  cfg->max_load               = 5;
  cfg->memcxt                 = *memcxt; 
}



void linhash_init(linhash_t* lhtbl, memcxt_t* memcxt){
  linhash_cfg_t* lhtbl_cfg;
  size_t index;
  
  assert((lhtbl != NULL) && (memcxt != NULL));

  lhtbl_cfg = &lhtbl->cfg;
  
  linhash_cfg_init(lhtbl_cfg, memcxt);

  assert(is_power_of_two(lhtbl_cfg->segment_size));

  assert(is_power_of_two(lhtbl_cfg->initial_directory_size));

  

  // lock for resolving contention  (only when cfg->multithreaded)      
  if(lhtbl_cfg->multithreaded){
    pthread_mutex_init(&lhtbl->mutex, NULL);
  }

  lhtbl->directory_size = lhtbl_cfg->initial_directory_size;
  lhtbl->directory_current = lhtbl->directory_size;
  
  // the array of segment pointers
  lhtbl->directory = memcxt->calloc(lhtbl->directory_size, sizeof(segmentptr));

  // mininum number of bins    [{ N }]                                     
  lhtbl->N = mul_size(lhtbl_cfg->segment_size, lhtbl->directory_current);

  // the number of times the table has doubled in size  [{ L }]            
  lhtbl->L = 0;

  // index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }] 
  lhtbl->p = 0;

  // the total number of records in the table                              
  lhtbl->count = 0;

  // the current limit on the bucket count  [{ N * 2^L }]                  
  lhtbl->maxp = lhtbl->N;

  // the current number of buckets
  lhtbl->currentsize = lhtbl->N;

  // create the segments needed by the current directory
  for(index = 0; index < lhtbl->directory_current; index++){
    lhtbl->directory[index] = memcxt->calloc(lhtbl_cfg->segment_size, sizeof(bucketptr));
  }

  
}



void delete_linhash(linhash_t* lhtbl){
  size_t segsz;
  size_t segindex;
  size_t index;
  segmentptr current_segment;
  bucketptr current_bucket;
  memcxt_t *memcxt;

  segsz = lhtbl->cfg.segment_size;

  memcxt = &lhtbl->cfg.memcxt;

  for(segindex = 0; segindex < lhtbl->directory_current; segindex++){

    current_segment = lhtbl->directory[index];
    
    //need to cdr down the segment and free the linked list of buckets
      for(index = 0; index < segsz; index++){
	current_bucket = current_segment[index];
	


      }
    //now free the segment
    memcxt->free( current_segment );
  }

  memcxt->free(lhtbl->directory);
  
}


/* 
 * BD's Jenkins's lookup3 code 
 */

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define final(a,b,c)      \
{                         \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/*
 * BD's: Hash code for a 64bit integer
 */
uint32_t jenkins_hash_uint64(uint64_t x) {
  uint32_t a, b, c;

  a = (uint32_t) x; // low order bits
  b = (uint32_t) (x >> 32); // high order bits
  c = 0xdeadbeef;
  final(a, b, c);

  return c;
}


/*
 * BD's: Hash code for an arbitrary pointer p
 */
uint32_t jenkins_hash_ptr(const void *p) {
  return jenkins_hash_uint64((uint64_t) ((size_t) p));
}


/* returns the raw offset/index of the bucket that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* htbl, const void *p){
  uint32_t jhash = jenkins_hash_ptr(p);
  uint32_t l = MOD(jhash, htbl->maxp);

  if(l < htbl->p){
    
    l = MOD(jhash, 2 * htbl->maxp);
    
  }
  
  return l;
  
}

bucketptr* offset2bucketptr(linhash_t* htbl, uint32_t offset){
  segmentptr segment;
  size_t segsz;
  uint32_t index;

  segsz = htbl->cfg.segment_size;
  
  segment = htbl->directory[offset / segsz];

  index = MOD(offset, segsz);

  return &segment[index];

}


/* 
 *  pointer (in the appropriate segment) to the bucketptr 
 *  where the start of the bucket chain should be for p 
 */
bucketptr* linhash_fetch_bucket(linhash_t* htbl, const void *p){
  uint32_t offset;
  
  offset = linhash_offset(htbl, p);

  return offset2bucketptr(htbl, offset);

}




static void linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt){
  size_t index;
  size_t oldsz;
  size_t newsz;
  segmentptr* olddir;
  segmentptr* newdir;
  
  oldsz = lhtbl->directory_size;
  
  /* we should be full */
  assert(oldsz == lhtbl->directory_current);

  newsz = oldsz  << 1;

  olddir = lhtbl->directory;

  newdir = lhtbl->cfg.memcxt.calloc(newsz, sizeof(segmentptr));

  for(index = 0; index < oldsz; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_size = newsz;

  lhtbl->cfg.memcxt.free(olddir);

}


static void linhash_expand_table(linhash_t* lhtbl, memcxt_t* memcxt){
  size_t segsz;

  size_t newaddr;
  size_t newindex;

  bucketptr* oldbucketp;

  bucketptr current;
  bucketptr previous;
  bucketptr lastofnew;
  
  segmentptr newseg;

  size_t newsegindex;

  newaddr = add_size(lhtbl->maxp, lhtbl->p);
 
  if(newaddr < mul_size(lhtbl->directory_current, lhtbl->cfg.segment_size)){

    oldbucketp = offset2bucketptr(lhtbl, lhtbl->p);

    segsz = lhtbl->cfg.segment_size;

    newindex = newaddr / segsz;

    /* expand address space; if necessary create new segment */
    if(MOD(newaddr, segsz) == 0){
      if(lhtbl->directory[newindex] == NULL){
	lhtbl->directory[newindex] = lhtbl->cfg.memcxt.calloc(segsz, sizeof(bucketptr));
      }
    }

    /* location of the new bucket */
    newseg = lhtbl->directory[newindex];
    newsegindex = MOD(newaddr, segsz);
      
    /* update the state variables */
    lhtbl->p += 1;
    if(lhtbl->p == lhtbl->maxp){
      lhtbl->maxp = lhtbl->maxp << 1;
      lhtbl->p = 0;
      lhtbl->L += 1;  /* not needed? */
    }

    lhtbl->currentsize += 1;

    /* now to split the buckets */
    current = *oldbucketp;
    previous = NULL;
    lastofnew = NULL;

    assert( newseg[newsegindex] == NULL );

    while( current != NULL ){

      if(linhash_offset(lhtbl, current->value) == newaddr){
	/* it belongs in the new bucket */

	if( lastofnew == NULL ){

	  newseg[newsegindex] = current;
	  
	} else {

	  lastofnew->next_bucket = current;
	  
	}

	if( previous == NULL ){

	  *oldbucketp = current->next_bucket;
	  
	  
	} else {

	  previous->next_bucket = current->next_bucket;

	}

	lastofnew = current;
	current = current->next_bucket;
	lastofnew->next_bucket = NULL;
	

      } else {
	/* it belongs in the old bucket */

	previous = current;
	current = current->next_bucket;


      }


    }
    
  } 
  
}


void linhash_insert(linhash_t* htbl, const void *p){


}

void *linhash_lookup(linhash_t* htbl, const void *p){


  return NULL;
}



