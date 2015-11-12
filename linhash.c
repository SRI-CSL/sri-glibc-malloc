#include <assert.h>
#include <errno.h>

#include "linhash.h"
#include "hashfns.h"
#include "memcxt.h"


const bool     linhash_multithreaded           = false;

const size_t   linhash_segment_size            = SEGMENT_LENGTH;
const size_t   linhash_initial_directory_size  = DIRECTORY_LENGTH;
const size_t   linhash_segments_at_startup     = 1;

const uint16_t  linhash_min_load                = 2;   
const uint16_t  linhash_max_load                = 5;


/* static routines */
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* linhash expansion routines */
static bool linhash_expand_check(linhash_t* lhtbl);

static bool linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);

/* split's the current bin  */
static bool linhash_expand_table(linhash_t* lhtbl);
 
/* returns the raw offset/index of the bucket that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* lhtbl, const void *p);

/* returns a pointer to the top bucket at the given offset  */
static bucketptr* offset2bucketptr(linhash_t* lhtbl, uint32_t offset);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucketptr bucket);

/* for sanity checking */
#ifndef NDEBUG
static bool is_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}
#endif

/* Fast modulo arithmetic, assuming that y is a power of 2 */
static inline size_t mod_power_of_two(size_t x, size_t y){
  assert(is_power_of_two(y));
  return x & (y - 1);
}


static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt){
  cfg->multithreaded          = linhash_multithreaded;
  cfg->segment_size           = linhash_segment_size;
  cfg->initial_directory_size = linhash_initial_directory_size;
  cfg->min_load               = linhash_min_load;   
  cfg->max_load               = linhash_max_load;
  cfg->memcxt                 = memcxt;
  cfg->directory_size_max     = UINT32_MAX;
  cfg->address_max            = mul_size(cfg->directory_size_max, cfg->segment_size);
}



bool init_linhash(linhash_t* lhtbl, memcxt_t* memcxt){
  linhash_cfg_t* lhtbl_cfg;
  size_t index;
  segmentptr seg;
  
  if((lhtbl == NULL) || (memcxt == NULL)){
    errno = EINVAL;
    return false;
  }
  
  assert((lhtbl != NULL) && (memcxt != NULL));

  lhtbl_cfg = &lhtbl->cfg;
  
  linhash_cfg_init(lhtbl_cfg, memcxt);
    
  //iam: should these be more than just asserts?
  assert(is_power_of_two(lhtbl_cfg->segment_size));

  assert(is_power_of_two(lhtbl_cfg->initial_directory_size));


  /* lock for resolving contention  (only when cfg->multithreaded)   */
  if(lhtbl_cfg->multithreaded){
    pthread_mutex_init(&lhtbl->mutex, NULL);
  }

  lhtbl->directory_size = lhtbl_cfg->initial_directory_size;
  lhtbl->directory_current = linhash_segments_at_startup; 

  
  /* the array of segment pointers */
  lhtbl->directory = memcxt->allocate(DIRECTORY, mul_size(lhtbl->directory_size, sizeof(segmentptr)));
  if(lhtbl->directory == NULL){
    errno = ENOMEM;
    return false;
  }
  
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

  assert(is_power_of_two(lhtbl->maxp));

  /* create the segments needed by the current directory */
  for(index = 0; index < lhtbl->directory_current; index++){
    seg = (segmentptr)memcxt->allocate(SEGMENT, sizeof(segment_t));
    lhtbl->directory[index] = seg;
    if(seg == NULL){
      errno = ENOMEM;
      return false;
    }
  }

  return true;
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
  memcxt = lhtbl->cfg.memcxt;

  for(segindex = 0; segindex < lhtbl->directory_current; segindex++){

    current_segment = lhtbl->directory[segindex];
    
    /* cdr down the segment and release the linked list of buckets */
      for(index = 0; index < segsz; index++){
	current_bucket = current_segment->segment[index];

	while(current_bucket != NULL){

	  next_bucket = current_bucket->next_bucket;
	  memcxt->release(BUCKET, current_bucket, sizeof(bucket_t));
	  current_bucket = next_bucket;
	}
      }
      /* now release the segment */
      memcxt->release(SEGMENT, current_segment,  sizeof(segment_t));
  }
  
  memcxt->release(DIRECTORY, lhtbl->directory, mul_size(lhtbl->directory_size, sizeof(segmentptr)));
}


/* returns the raw offset/index of the bin that should contain p  [{ hash }] */
static uint32_t linhash_offset(linhash_t* lhtbl, const void *p){
  uint32_t jhash;
  uint32_t l;
  size_t next_maxp;

  
  jhash  = jenkins_hash_ptr(p);

  l = mod_power_of_two(jhash, lhtbl->maxp);

  if(l < lhtbl->p){

    next_maxp = lhtbl->maxp << 1;

    assert(next_maxp < lhtbl->cfg.directory_size_max);
	   
    l = mod_power_of_two(jhash, next_maxp);
  }
  
  return l;
}

bucketptr* offset2bucketptr(linhash_t* lhtbl, uint32_t offset){
  segmentptr segptr;
  size_t segsz;
  size_t segindex;
  uint32_t index;

  assert( offset < lhtbl->currentsize );
  
  segsz = lhtbl->cfg.segment_size;

  segindex = offset / segsz;

  assert( segindex < lhtbl->directory_current );

  segptr = lhtbl->directory[segindex];

  index = mod_power_of_two(offset, segsz);

  return &(segptr->segment[index]);
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

/* check if the table needs to be expanded; true if either it didn't need to be 
 * expanded, or else it expanded successfully. false if it failed.
 *
 */
bool linhash_expand_check(linhash_t* lhtbl){
  if(lhtbl->count / lhtbl->currentsize > lhtbl->cfg.max_load){
    return linhash_expand_table(lhtbl);
  }
  return true;
}


static bool linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt){
  size_t index;
  size_t oldsz;
  size_t newsz;
  segmentptr* olddir;
  segmentptr* newdir;

  oldsz = lhtbl->directory_size;
  
  /* we should be full */
  assert(oldsz == lhtbl->directory_current);

  newsz = oldsz  << 1;

  assert(newsz  < lhtbl->cfg.directory_size_max);

  olddir = lhtbl->directory;

  //iam: sz and len need to use these two accurately throughout.
  //fix when stable.
  newdir = memcxt->allocate(DIRECTORY, mul_size(newsz, sizeof(segmentptr)));
  if(newdir == NULL){
    errno = ENOMEM;
    return false;
  }

  //DD could try to do realloc and if we succeed we do not need to copy ...
  
  for(index = 0; index < oldsz; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_size = newsz;

  memcxt->release(DIRECTORY, olddir, mul_size(oldsz, sizeof(segmentptr)));

  return true;
}


static bool linhash_expand_table(linhash_t* lhtbl){
  size_t segsz;
  size_t newaddr;
  size_t newindex;
  bucketptr* oldbucketp;
  bucketptr current;
  bucketptr previous;
  bucketptr lastofnew;
  segmentptr newseg;
  size_t newsegindex;
  memcxt_t *memcxt;

  memcxt = lhtbl->cfg.memcxt;

  newaddr = add_size(lhtbl->maxp, lhtbl->p);

  assert(newaddr < lhtbl->cfg.address_max);

  /* see if the directory needs to grow  */
  if(lhtbl->directory_size  ==  lhtbl->directory_current){
    if(! linhash_expand_directory(lhtbl,  memcxt)){
      return false;
    }
  }


  if(newaddr < lhtbl->cfg.address_max){

    oldbucketp = offset2bucketptr(lhtbl, lhtbl->p);

    segsz = lhtbl->cfg.segment_size;

    newindex = newaddr / segsz;

    newsegindex = mod_power_of_two(newaddr, segsz);  
    
    /* expand address space; if necessary create new segment */  
    if((newsegindex == 0) && (lhtbl->directory[newindex] == NULL)){
      newseg = memcxt->allocate(SEGMENT, sizeof(segment_t));
      if(newseg == NULL){
	errno = ENOMEM;
	return false;
      }
      lhtbl->directory[newindex] = newseg;
      lhtbl->directory_current += 1;
    }

    /* location of the new bucket */
    newseg = lhtbl->directory[newindex];
   
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
	if( lastofnew == NULL ){      //BD & DD should preserve the order of the buckets in BOTH the old and new bins
	  newseg->segment[newsegindex] = current;
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

  return true;
}




/* iam Q1: what should we do if the thing is already in the table ?            */
/* iam Q2: should we insert at the front or back or ...                        */
/* iam Q3: how often should we check to see if the table needs to be expanded  */

bool linhash_insert(linhash_t* lhtbl, const void *key, const void *value){
  bucketptr newbucket;
  bucketptr* binp;

  binp = linhash_fetch_bucket(lhtbl, key);

  newbucket = lhtbl->cfg.memcxt->allocate(BUCKET, sizeof(bucket_t));
  if(newbucket == NULL){
    errno = ENOMEM;
    return false;
  }
  
  newbucket->key = (void *)key;
  newbucket->value = (void *)value;

  /* for the time being we insert the bucket at the front */
  newbucket->next_bucket = *binp;
  *binp = newbucket;

  /* census adjustments */
  lhtbl->count++;

  /* check to see if we need to exand the table */
  return linhash_expand_check(lhtbl);
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
      lhtbl->cfg.memcxt->release(BUCKET, current_bucketp, sizeof(bucket_t));

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
      lhtbl->cfg.memcxt->release(BUCKET, temp_bucketp, sizeof(bucket_t));
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
