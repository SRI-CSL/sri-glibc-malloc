#include <assert.h>
#include <errno.h>

#include "linhash.h"
#include "hashfns.h"
#include "memcxt.h"


const bool      linhash_multithreaded             = false;

const size_t    linhash_segment_length            = SEGMENT_LENGTH;
const size_t    linhash_initial_directory_length  = DIRECTORY_LENGTH;
const size_t    linhash_segments_at_startup       = 1;

const uint16_t  linhash_min_load                 = 2;   
const uint16_t  linhash_max_load                 = 5;


/* static routines */
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* linhash expansion routines */
static bool linhash_expand_check(linhash_t* lhtbl);

static bool linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);

/* split's the current bin  */
static bool linhash_expand_table(linhash_t* lhtbl);
 
/* returns the bin index (bindex) of the bin that should contain p  [{ hash }] */
static uint32_t linhash_bindex(linhash_t* lhtbl, const void *p);

/* returns a pointer to the top bucket at the given bindex  */
static bucketptr* bindex2bucketptr(linhash_t* lhtbl, uint32_t bindex);

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
  cfg->multithreaded            = linhash_multithreaded;
  cfg->segment_length           = linhash_segment_length;
  cfg->initial_directory_length = linhash_initial_directory_length;
  cfg->min_load                 = linhash_min_load;   
  cfg->max_load                 = linhash_max_load;
  cfg->memcxt                   = memcxt;
  cfg->bincount_max             = UINT32_MAX;
  cfg->directory_length_max     = cfg->bincount_max / SEGMENT_LENGTH;

  //iam: should these be more than just asserts?
  assert(is_power_of_two(cfg->segment_length));
  assert(is_power_of_two(cfg->initial_directory_length));

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
    

  /* lock for resolving contention  (only when cfg->multithreaded)   */
  if(lhtbl_cfg->multithreaded){
    pthread_mutex_init(&lhtbl->mutex, NULL);
  }

  lhtbl->directory_length = lhtbl_cfg->initial_directory_length;
  lhtbl->directory_current = linhash_segments_at_startup; 

  
  /* the array of segment pointers */
  lhtbl->directory = memcxt->allocate(DIRECTORY, mul_size(lhtbl->directory_length, sizeof(segmentptr)));
  if(lhtbl->directory == NULL){
    errno = ENOMEM;
    return false;
  }
  
  /* mininum number of bins    [{ N }]   */
  lhtbl->N = mul_size(lhtbl_cfg->segment_length, lhtbl->directory_current);

  /* the number of times the table has doubled in size  [{ L }]   */
  lhtbl->L = 0;

  /* index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }] */
  lhtbl->p = 0;

  /* the total number of records in the table  */
  lhtbl->count = 0;

  /* the current limit on the bucket count  [{ N * 2^L }] */
  lhtbl->maxp = lhtbl->N;

  /* the current number of buckets */
  lhtbl->bincount = lhtbl->N;

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
  
  fprintf(fp, "directory_length = %lu\n", (unsigned long)lhtbl->directory_length);
  fprintf(fp, "directory_current = %lu\n", (unsigned long)lhtbl->directory_current);
  fprintf(fp, "N = %lu\n", (unsigned long)lhtbl->N);
  fprintf(fp, "L = %lu\n", (unsigned long)lhtbl->L);
  fprintf(fp, "count = %lu\n", (unsigned long)lhtbl->count);
  fprintf(fp, "maxp = %lu\n", (unsigned long)lhtbl->maxp);
  fprintf(fp, "bincount = %lu\n", (unsigned long)lhtbl->bincount);
  fprintf(fp, "load = %d\n", (int)(lhtbl->count / lhtbl->bincount));

  if(showloads){
    fprintf(fp, "bucket lengths: ");
    for(index = 0; index < lhtbl->bincount; index++){
      bp = bindex2bucketptr(lhtbl, index);
      blen = bucket_length(*bp);
      if(blen != 0){
	fprintf(fp, "%zu:%zu ", index, blen);
      }
    }
    fprintf(fp, "\n");
  }
  
}


void delete_linhash(linhash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  size_t index;
  segmentptr current_segment;
  bucketptr current_bucket;
  bucketptr next_bucket;
  memcxt_t *memcxt;

  seglen = lhtbl->cfg.segment_length;
  memcxt = lhtbl->cfg.memcxt;

  for(segindex = 0; segindex < lhtbl->directory_current; segindex++){

    current_segment = lhtbl->directory[segindex];
    
    /* cdr down the segment and release the linked list of buckets */
      for(index = 0; index < seglen; index++){
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
  
  memcxt->release(DIRECTORY, lhtbl->directory, mul_size(lhtbl->directory_length, sizeof(segmentptr)));
}


/* returns the raw bindex/index of the bin that should contain p  [{ hash }] */
static uint32_t linhash_bindex(linhash_t* lhtbl, const void *p){
  uint32_t jhash;
  uint32_t l;
  size_t next_maxp;

  
  jhash  = jenkins_hash_ptr(p);

  l = mod_power_of_two(jhash, lhtbl->maxp);

  if(l < lhtbl->p){

    next_maxp = lhtbl->maxp << 1;

    assert(next_maxp < lhtbl->cfg.directory_length_max);
	   
    l = mod_power_of_two(jhash, next_maxp);
  }
  
  return l;
}

bucketptr* bindex2bucketptr(linhash_t* lhtbl, uint32_t bindex){
  segmentptr segptr;
  size_t seglen;
  size_t segindex;
  uint32_t index;

  assert( bindex < lhtbl->bincount );
  
  seglen = lhtbl->cfg.segment_length;

  segindex = bindex / seglen;

  assert( segindex < lhtbl->directory_current );

  segptr = lhtbl->directory[segindex];

  index = mod_power_of_two(bindex, seglen);

  return &(segptr->segment[index]);
}


/* 
 *  pointer (in the appropriate segment) to the bucketptr 
 *  where the start of the bucket chain should be for p 
 */
bucketptr* linhash_fetch_bucket(linhash_t* lhtbl, const void *p){
  uint32_t bindex;
  
  bindex = linhash_bindex(lhtbl, p);

  return bindex2bucketptr(lhtbl, bindex);
}

/* check if the table needs to be expanded; true if either it didn't need to be 
 * expanded, or else it expanded successfully. false if it failed.
 *
 */
bool linhash_expand_check(linhash_t* lhtbl){
  if((lhtbl->bincount < lhtbl->cfg.bincount_max) && (lhtbl->count / lhtbl->bincount > lhtbl->cfg.max_load)){
    return linhash_expand_table(lhtbl);
  }
  return true;
}


static bool linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt){
  size_t index;
  size_t old_dirlen;
  size_t new_dirlen;
  segmentptr* olddir;
  segmentptr* newdir;

  old_dirlen = lhtbl->directory_length;
  
  /* we should be full */
  assert(old_dirlen == lhtbl->directory_current);

  new_dirlen = old_dirlen  << 1;

  assert(new_dirlen  < lhtbl->cfg.directory_length_max);

  olddir = lhtbl->directory;

  newdir = memcxt->allocate(DIRECTORY, mul_size(new_dirlen, sizeof(segmentptr)));
  if(newdir == NULL){
    errno = ENOMEM;
    return false;
  }

  //DD could try to do realloc and if we succeed we do not need to copy ...
  
  for(index = 0; index < old_dirlen; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_length = new_dirlen;

  memcxt->release(DIRECTORY, olddir, mul_size(old_dirlen, sizeof(segmentptr)));

  return true;
}


static bool linhash_expand_table(linhash_t* lhtbl){
  size_t seglen;
  size_t new_bindex;
  size_t new_segindex;
  bucketptr* oldbucketp;
  bucketptr current;
  bucketptr previous;
  bucketptr lastofnew;
  segmentptr newseg;
  size_t newsegindex;
  memcxt_t *memcxt;

  memcxt = lhtbl->cfg.memcxt;

  new_bindex = add_size(lhtbl->maxp, lhtbl->p);

  assert(new_bindex < lhtbl->cfg.bincount_max);

  /* see if the directory needs to grow  */
  if(lhtbl->directory_length  ==  lhtbl->directory_current){
    if(! linhash_expand_directory(lhtbl,  memcxt)){
      return false;
    }
  }


  if(new_bindex < lhtbl->cfg.bincount_max){

    oldbucketp = bindex2bucketptr(lhtbl, lhtbl->p);

    seglen = lhtbl->cfg.segment_length;

    new_segindex = new_bindex / seglen;

    newsegindex = mod_power_of_two(new_bindex, seglen);  
    
    /* expand address space; if necessary create new segment */  
    if((newsegindex == 0) && (lhtbl->directory[new_segindex] == NULL)){
      newseg = memcxt->allocate(SEGMENT, sizeof(segment_t));
      if(newseg == NULL){
	errno = ENOMEM;
	return false;
      }
      lhtbl->directory[new_segindex] = newseg;
      lhtbl->directory_current += 1;
    }

    /* location of the new bin */
    newseg = lhtbl->directory[new_segindex];
   
    /* update the state variables */
    lhtbl->p += 1;
    if(lhtbl->p == lhtbl->maxp){
      lhtbl->maxp = lhtbl->maxp << 1;
      lhtbl->p = 0;
      lhtbl->L += 1; 
    }

    lhtbl->bincount += 1;

    /* now to split the buckets */
    current = *oldbucketp;
    previous = NULL;
    lastofnew = NULL;

    assert( newseg[newsegindex] == NULL );

    while( current != NULL ){

      if(linhash_bindex(lhtbl, current->key) == new_bindex){

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
