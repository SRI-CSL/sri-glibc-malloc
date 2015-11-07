#include "linhash.h"
#include "hashfns.h"

#include <assert.h>



const bool     linhash_multithreaded           = false;

const size_t   linhash_segment_size            = 16; //256;
const size_t   linhash_initial_directory_size  = 16; //256;
const size_t   linhash_segments_at_startup     = 1;  //256;

const int16_t  linhash_min_load                = 2;   
const int16_t  linhash_max_load                = 5;




/* static routines */
static bool is_power_of_two(uint32_t n);
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* returns the abstract offset/index of the bin that should contain p */
static uint32_t linhash_offset(linhash_t* lhtbl, const void *p);

/* linhash expansion routines */
static void linhash_expand_check(linhash_t* lhtbl);
static void linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);

/* split's the current bin; returns the number of buckets moved to the new bin */
static size_t linhash_expand_table(linhash_t* lhtbl);
 
/* returns the raw offset/index of the bucket that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* lhtbl, const void *p);

/* returns a pointer to the top bucket at the given offset  */
static bucketptr* offset2bucketptr(linhash_t* lhtbl, uint32_t offset);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucketptr bucket);


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

  
  /* lock for resolving contention  (only when cfg->multithreaded)   */
  if(lhtbl_cfg->multithreaded){
    pthread_mutex_init(&lhtbl->mutex, NULL);
  }

  lhtbl->directory_size = lhtbl_cfg->initial_directory_size;
  lhtbl->directory_current = linhash_initial_directory_size;  //iam: should be linhash_segments_at_startup

  /* the array of segment pointers */
  lhtbl->directory = memcxt->calloc(DIRECTORY, lhtbl->directory_size, sizeof(segmentptr));

  /* mininum number of bins    [{ N }]   */
  lhtbl->N = mul_size(lhtbl_cfg->segment_size, lhtbl->directory_current);

  /* the number of times the table has doubled in size  [{ L }]   */
  lhtbl->L = 0;

  /* index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }] */
  lhtbl->p = 0;

  /* the total number of records in the table  */
  lhtbl->count = 0;

  /* the current limit on the bucket count  [{ N * 2^L }] */
  lhtbl->maxp = lhtbl->N;

  /* the current number of buckets */
  lhtbl->currentsize = lhtbl->N;

  /* create the segments needed by the current directory */
  for(index = 0; index < lhtbl->directory_current; index++){ //iam: should be linhash_segments_at_startup
    lhtbl->directory[index] = memcxt->calloc(SEGMENT, lhtbl_cfg->segment_size, sizeof(bucketptr));
  }
}

extern void dump_linhash(FILE* fp, linhash_t* lhtbl, bool showloads){
  size_t index;
  size_t blen;
  bucketptr* bp;
  
  fprintf(fp, "directory_size = %lu\n", (unsigned long)lhtbl->directory_size);
  fprintf(fp, "directory_current = %lu\n", (unsigned long)lhtbl->directory_current);
  fprintf(fp, "N = %lu\n", (unsigned long)lhtbl->N);
  fprintf(fp, "L = %lu\n", (unsigned long)lhtbl->L);
  fprintf(fp, "count = %lu\n", (unsigned long)lhtbl->count);
  fprintf(fp, "maxp = %lu\n", (unsigned long)lhtbl->maxp);
  fprintf(fp, "currentsize = %lu\n", (unsigned long)lhtbl->currentsize);
  fprintf(fp, "load = %d\n", (int)(lhtbl->count / lhtbl->currentsize));

  if(showloads){
    fprintf(fp, "bucket lengths: ");
    for(index = 0; index < lhtbl->currentsize; index++){
      bp = offset2bucketptr(lhtbl, index);
      blen = bucket_length(*bp);
      if(blen != 0){
	fprintf(fp, "%zu:%zu ", index, blen);
      }
    }
    fprintf(fp, "\n");
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
    
    /* cdr down the segment and free the linked list of buckets */
      for(index = 0; index < segsz; index++){
	current_bucket = current_segment[index];

	while(current_bucket != NULL){

	  next_bucket = current_bucket->next_bucket;
	  memcxt->free(BUCKET, current_bucket );
	  current_bucket = next_bucket;
	}
      }
      /* now free the segment */
      memcxt->free(SEGMENT, current_segment );
  }
  
  memcxt->free(DIRECTORY, lhtbl->directory);
}


/* returns the raw offset/index of the bucket that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* lhtbl, const void *p){
  uint32_t jhash = jenkins_hash_ptr(p);
  uint32_t l = MOD(jhash, lhtbl->maxp);

  if(l < lhtbl->p){
    l = MOD(jhash, 2 * lhtbl->maxp);
  }
  
  return l;
}

bucketptr* offset2bucketptr(linhash_t* lhtbl, uint32_t offset){
  segmentptr segment;
  size_t segsz;
  size_t segindex;
  uint32_t index;

  assert( offset < lhtbl->currentsize );
  
  segsz = lhtbl->cfg.segment_size;

  segindex = offset / segsz;

  assert( segindex < lhtbl->directory_current );

  segment = lhtbl->directory[segindex];

  index = MOD(offset, segsz);

  return &segment[index];
}


/* 
 *  pointer (in the appropriate segment) to the bucketptr 
 *  where the start of the bucket chain should be for p 
 */
bucketptr* linhash_fetch_bucket(linhash_t* lhtbl, const void *p){
  uint32_t offset;
  
  offset = linhash_offset(lhtbl, p);

  return offset2bucketptr(lhtbl, offset);
}

/* check if the table needs to be expanded */
void linhash_expand_check(linhash_t* lhtbl){
  size_t moved;
  if(lhtbl->count / lhtbl->currentsize > lhtbl->cfg.max_load){
    moved = linhash_expand_table(lhtbl);
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

  newdir = memcxt->calloc(DIRECTORY, newsz, sizeof(segmentptr));

  for(index = 0; index < oldsz; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_size = newsz;

  memcxt->free(DIRECTORY, olddir);
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
	lhtbl->directory[newindex] = memcxt->calloc(SEGMENT, segsz, sizeof(bucketptr));
	lhtbl->directory_current += 1;
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
      lhtbl->L += 1; 
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
