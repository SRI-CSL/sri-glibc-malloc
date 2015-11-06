#include "linhash.h"

#include <assert.h>



const bool     linhash_multithreaded           = false;

const size_t   linhash_segment_size            = 16; //256;
const size_t   linhash_initial_directory_size  = 16; //256;
const size_t   linhash_segments_at_startup     = 16; //256;

const int16_t  linhash_min_load                = 2;   
const int16_t  linhash_max_load                = 5;




/* static routines */
static bool is_power_of_two(uint32_t n);
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* returns the abstract offset/index of the bin that should contain p */
static uint32_t linhash_offset(linhash_t* htbl, const void *p);

/* linhash expansion routines */
static void linhash_expand_check(linhash_t* lhtbl);
static void linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);

/* split's the current bin; returns the number of buckets moved to the new bin */
static size_t linhash_expand_table(linhash_t* lhtbl);

/* linhash contraction routines */
static void linhash_contract_check(linhash_t* lhtbl);
static void linhash_contract_directory(linhash_t* lhtbl, memcxt_t* memcxt);
static void linhash_contract_table(linhash_t* lhtbl);


/* returns the raw offset/index of the bucket that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* htbl, const void *p);

/* returns a pointer to the top bucket at the given offset  */
static bucketptr* offset2bucketptr(linhash_t* htbl, uint32_t offset);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucketptr bucket);


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
  cfg->multithreaded          = linhash_multithreaded;
  cfg->segment_size           = linhash_segment_size;
  cfg->initial_directory_size = linhash_initial_directory_size;
  cfg->min_load               = linhash_min_load;   
  cfg->max_load               = linhash_max_load;
  cfg->memcxt                 = *memcxt;
  cfg->directory_size_max     = UINT32_MAX;
  cfg->address_max            = mul_size(cfg->directory_size_max, cfg->segment_size);
}



void init_linhash(linhash_t* lhtbl, memcxt_t* memcxt){
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
  lhtbl->directory_current = linhash_initial_directory_size;
  
  // the array of segment pointers
  lhtbl->directory = memcxt->calloc(DIRECTORY, lhtbl->directory_size, sizeof(segmentptr));

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
    lhtbl->directory[index] = memcxt->calloc(SEGMENT, lhtbl_cfg->segment_size, sizeof(bucketptr));
  }

  
}

extern void dump_linhash(FILE* fp, linhash_t* lhtbl){
  
  fprintf(fp, "directory_size = %lu\n", (unsigned long)lhtbl->directory_size);
  fprintf(fp, "directory_current = %lu\n", (unsigned long)lhtbl->directory_current);
  fprintf(fp, "N = %lu\n", (unsigned long)lhtbl->N);
  fprintf(fp, "L = %lu\n", (unsigned long)lhtbl->L);
  fprintf(fp, "count = %lu\n", (unsigned long)lhtbl->count);
  fprintf(fp, "maxp = %lu\n", (unsigned long)lhtbl->maxp);
  fprintf(fp, "currentsize = %lu\n", (unsigned long)lhtbl->currentsize);
  fprintf(fp, "load = %d\n", (int)(lhtbl->count / lhtbl->currentsize));

  if(1){
    size_t index;
    bucketptr* bp;
    for(index = 0; index < lhtbl->currentsize; index++){
      bp = offset2bucketptr(lhtbl, index);
      fprintf(stderr, "%zu:%zu ", index, bucket_length(*bp));
    }
    fprintf(stderr, "\n");
  }
  
}


void delete_linhash(linhash_t* lhtbl){
  size_t segsz;
  size_t segindex;
  size_t index;
  segmentptr current_segment;
  bucketptr current_bucket;
  bucketptr next_bucket;
  memcxt_t *memcxt;

  segsz = lhtbl->cfg.segment_size;

  memcxt = &lhtbl->cfg.memcxt;

  for(segindex = 0; segindex < lhtbl->directory_current; segindex++){

    current_segment = lhtbl->directory[segindex];
    
    //need to cdr down the segment and free the linked list of buckets
      for(index = 0; index < segsz; index++){
	current_bucket = current_segment[index];

	while(current_bucket != NULL){

	  next_bucket = current_bucket->next_bucket;
	  memcxt->free(BUCKET, current_bucket );
	  current_bucket = next_bucket;
	  

	}


      }
    //now free the segment
      memcxt->free(SEGMENT, current_segment );
  }

  memcxt->free(DIRECTORY, lhtbl->directory);
  
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

/* check if the table needs to be expanded */
void linhash_expand_check(linhash_t* lhtbl){
  size_t moved;
  if(lhtbl->count / lhtbl->currentsize > lhtbl->cfg.max_load){
    moved = linhash_expand_table(lhtbl);
    //fprintf(stderr, "Expanded table moved = %zu\n", moved);
   }
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

  newdir = lhtbl->cfg.memcxt.calloc(DIRECTORY, newsz, sizeof(segmentptr));

  for(index = 0; index < oldsz; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_size = newsz;

  lhtbl->cfg.memcxt.free(DIRECTORY, olddir);

}


static size_t linhash_expand_table(linhash_t* lhtbl){
  size_t segsz;

  size_t newaddr;
  size_t newindex;

  size_t moved;
  
  bucketptr* oldbucketp;

  bucketptr current;
  bucketptr previous;
  bucketptr lastofnew;
  
  segmentptr newseg;

  size_t newsegindex;

  memcxt_t *memcxt;

  memcxt = &lhtbl->cfg.memcxt;

  
  /* see if the directory needs to grow  */
  if(lhtbl->directory_size  ==  lhtbl->directory_current){
    fprintf(stderr, "Expanding direcctory\n");
    linhash_expand_directory(lhtbl,  memcxt);
  }

  moved = 0;
  
  newaddr = add_size(lhtbl->maxp, lhtbl->p);

  if(newaddr < lhtbl->cfg.address_max){

    oldbucketp = offset2bucketptr(lhtbl, lhtbl->p);

    segsz = lhtbl->cfg.segment_size;

    newindex = newaddr / segsz;

    /* expand address space; if necessary create new segment */
    if(MOD(newaddr, segsz) == 0){
      if(lhtbl->directory[newindex] == NULL){
	lhtbl->directory[newindex] = lhtbl->cfg.memcxt.calloc(SEGMENT, segsz, sizeof(bucketptr));
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

      if(linhash_offset(lhtbl, current->key) == newaddr){
	/* it belongs in the new bucket */

	moved++;
	
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

  return moved;
  
}



static void linhash_contract_check(linhash_t* lhtbl){
  /* iam Q4: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (lhtbl->count / lhtbl->currentsize < lhtbl->cfg.min_load)){
      linhash_contract_table(lhtbl);
    }
}

static void linhash_contract_directory(linhash_t* lhtbl, memcxt_t* memcxt){

}

static void linhash_contract_table(linhash_t* lhtbl){

  memcxt_t *memcxt;

  memcxt = &lhtbl->cfg.memcxt;

  /* see if the directory needs to contract; iam Q5 need to ensure we don't get unwanted oscilations  */
  if(lhtbl->directory_size  >  (lhtbl->directory_current >> 1)){
    linhash_contract_directory(lhtbl,  memcxt);
  }

}

/* iam Q1: what should we do if the thing is already in the table ?            */
/* iam Q2: should we insert at the front or back or ...                        */
/* iam Q3: how often should we check to see if the table needs to be expanded  */

void linhash_insert(linhash_t* lhtbl, const void *key, const void *value){
  bucketptr newbucket;
  bucketptr* binp;

  binp = linhash_fetch_bucket(lhtbl, key);

  newbucket = lhtbl->cfg.memcxt.malloc(BUCKET, sizeof(bucket_t));

  newbucket->key = (void *)key;
  newbucket->value = (void *)value;

  /* for the time being we insert the bucket at the front */
  newbucket->next_bucket = *binp;
  *binp = newbucket;

  /* census adjustments */
  lhtbl->count++;

  /* check to see if we need to exand the table; Q5: not really needed for EVERY insert */
  linhash_expand_check(lhtbl);

}

void *linhash_lookup(linhash_t* lhtbl, const void *key){
  void* value;
  bucketptr* binp;
  bucketptr bucketp;

  value = NULL;
  binp = linhash_fetch_bucket(lhtbl, key);
  bucketp = *binp;

  while(bucketp != NULL){
    if(key == bucketp->key){
      value = bucketp->value;
      break;
    }
    bucketp = bucketp->next_bucket;
  }

  return value;
}

bool linhash_delete(linhash_t* lhtbl, const void *key){
  bool found = false;
  bucketptr* binp;
  bucketptr current_bucketp;
  bucketptr previous_bucketp;

  previous_bucketp = NULL;
  binp = linhash_fetch_bucket(lhtbl, key);
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    if(key == current_bucketp->key){
      found = true;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      lhtbl->cfg.memcxt.free(BUCKET, current_bucketp);

      /* census adjustments */
      lhtbl->count--;

      break;
    }
    previous_bucketp = current_bucketp;
    current_bucketp = current_bucketp->next_bucket;
  }

  /* check to see if we can contract the table; iam Q6: not really needed for EVERY delete */
  linhash_contract_check(lhtbl);

  return found;
}

size_t linhash_delete_all(linhash_t* lhtbl, const void *key){
  size_t count;
  bucketptr* binp;
  bucketptr current_bucketp;
  bucketptr previous_bucketp;
  bucketptr temp_bucketp;

  count = 0;
  previous_bucketp = NULL;
  binp = linhash_fetch_bucket(lhtbl, key);
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    if(key == current_bucketp->key){
      count++;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      temp_bucketp = current_bucketp;
      current_bucketp = current_bucketp->next_bucket;
      lhtbl->cfg.memcxt.free(BUCKET, temp_bucketp);
    } else {
      previous_bucketp = current_bucketp;
      current_bucketp = current_bucketp->next_bucket;
    }
  }

  /* census adjustments */
  lhtbl->count -= count;

  /* check to see if we can contract the table; not really needed for EVERY delete */
  linhash_contract_check(lhtbl);

  return count;
}


size_t bucket_length(bucketptr bucket){
  size_t count;
  bucketptr current;

  count = 0;
  current = bucket;
  while(current != NULL){
    count++;
    current = current->next_bucket;
  }

  return count;
}
