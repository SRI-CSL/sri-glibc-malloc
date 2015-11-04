#include "linhash.h"

#include <assert.h>


static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt){
  cfg->multithreaded          = false;
  cfg->segment_size           = 256;
  cfg->initial_directory_size = 256;
  cfg->min_load               = 2;   
  cfg->max_load               = 5;
  cfg->memcxt                 = *memcxt; 
}



void linhash_init(linhash_t* lhash, memcxt_t* memcxt){
  linhash_cfg_t* lhash_cfg;
  size_t index;
  
  assert((lhash != NULL) && (memcxt != NULL));

  lhash_cfg = &lhash->cfg;
  
  linhash_cfg_init(lhash_cfg, memcxt);

  // lock for resolving contention  (only when cfg->multithreaded)      
  if(lhash_cfg->multithreaded){
    pthread_mutex_init(&lhash->mutex, NULL);
  }

  lhash->directory_size = lhash_cfg->initial_directory_size;
  lhash->directory_current = lhash->directory_size;
  
  // the array of segment pointers
  lhash->directory = memcxt->calloc(lhash->directory_size, sizeof(segmentptr));

  // mininum number of bins    [{ N }]                                     
  lhash->N = mul_size(lhash_cfg->segment_size, lhash->directory_current);

  // the number of times the table has doubled in size  [{ L }]            
  lhash->L = 0;

  // index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }] 
  lhash->p = 0;

  // the total number of records in the table                              
  lhash->count = 0;

  // the current limit on the bucket count  [{ N * 2^L }]                  
  lhash->maxp = lhash->N;

  // create the segments needed by the current directory
  for(index = 0; index < lhash->directory_current; index++){
    lhash->directory[index] = memcxt->calloc(lhash_cfg->segment_size, sizeof(bucketptr));
  }

  
}

/*
 * Fast MOD arithmetic, assuming that y is a power of 2 !
 */
#define MOD(x,y)   ((x) & ((y)-1))



/* 
 * BD's Jenkins's lookup3 code 
 */

/*
 * BD's: Hash code for a 32bit integer
 */
uint32_t jenkins_hash_uint32(uint32_t x) {
  x = (x + 0x7ed55d16) + (x<<12);
  x = (x ^ 0xc761c23c) ^ (x>>19);
  x = (x + 0x165667b1) + (x<<5);
  x = (x + 0xd3a2646c) ^ (x<<9);
  x = (x + 0xfd7046c5) + (x<<3);
  x = (x ^ 0xb55a4f09) ^ (x>>16);

  return x;
}

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



uint32_t linhash_bucket(linhash_t* htbl, const void *p){
  uint32_t jhash = jenkins_hash_ptr(p);
  uint32_t l = MOD(jhash, htbl->maxp);

  if(l < htbl->p){
    
    l = MOD(jhash, 2 * htbl->maxp);
    
  }
  
  return l;
  
}
