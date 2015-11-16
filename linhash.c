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

/* returns a pointer to the bin i.e. the top bucket at the given bindex  */
static bucket_t** bindex2bin(linhash_t* lhtbl, uint32_t bindex);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucket_t* bucket);

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
  segment_t* seg;
  bool success;
  size_t dirsz;
  size_t binsz;
  
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

  
  /* the size of the directory */
  success = mul_size(lhtbl->directory_length, sizeof(segment_t*), &dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
    
  /* the directory; i.e. the array of segment pointers */
  lhtbl->directory = memcxt->allocate(DIRECTORY, dirsz);
  if(lhtbl->directory == NULL){
    errno = ENOMEM;
    return false;
  }
  
  /* mininum number of bins    [{ N }]   */
  success = mul_size(lhtbl_cfg->segment_length, lhtbl->directory_current, &binsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
  lhtbl->N = binsz;

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
    seg = (segment_t*)memcxt->allocate(SEGMENT, sizeof(segment_t));
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
  bucket_t** bp;

  size_t maxlength;
  

  
  
  fprintf(fp, "directory_length = %lu\n", (unsigned long)lhtbl->directory_length);
  fprintf(fp, "directory_current = %lu\n", (unsigned long)lhtbl->directory_current);
  fprintf(fp, "N = %lu\n", (unsigned long)lhtbl->N);
  fprintf(fp, "L = %lu\n", (unsigned long)lhtbl->L);
  fprintf(fp, "count = %lu\n", (unsigned long)lhtbl->count);
  fprintf(fp, "maxp = %lu\n", (unsigned long)lhtbl->maxp);
  fprintf(fp, "bincount = %lu\n", (unsigned long)lhtbl->bincount);
  fprintf(fp, "load = %d\n", (int)(lhtbl->count / lhtbl->bincount));

  
  maxlength = 0;
  
  if(showloads){
    fprintf(fp, "bucket lengths: ");
  }

  for(index = 0; index < lhtbl->bincount; index++){
    bp = bindex2bin(lhtbl, index);
    blen = bucket_length(*bp);
    if( blen > maxlength ){  maxlength = blen; }
    if(showloads && blen != 0){
      fprintf(fp, "%zu:%zu ", index, blen);
    }
  }
  fprintf(fp, "\n");
  fprintf(fp, "maximum length = %zu\n", maxlength);
}


void delete_linhash(linhash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  size_t index;
  segment_t* current_segment;
  bucket_t* current_bucket;
  bucket_t* next_bucket;
  memcxt_t *memcxt;
  size_t dirsz;
  bool success;

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
  
  success = mul_size(lhtbl->directory_length, sizeof(segment_t*), &dirsz);
  assert(success);
  if(success){
    memcxt->release(DIRECTORY, lhtbl->directory, dirsz);
  }
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

    /* if we can expand to next_maxp we use that */
    if(next_maxp < lhtbl->cfg.bincount_max){
      l = mod_power_of_two(jhash, next_maxp);
    }
    
  }
  
  return l;
}

bucket_t** bindex2bin(linhash_t* lhtbl, uint32_t bindex){
  segment_t* segptr;
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
 *  pointer (in the appropriate segment) to the bucket_t* 
 *  where the start of the bucket chain should be for p 
 */
bucket_t** linhash_fetch_bucket(linhash_t* lhtbl, const void *p){
  uint32_t bindex;
  
  bindex = linhash_bindex(lhtbl, p);

  return bindex2bin(lhtbl, bindex);
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
  size_t old_dirsz;
  size_t new_dirlen;
  size_t new_dirsz;
  bool success;
  segment_t** olddir;
  segment_t** newdir;

  old_dirlen = lhtbl->directory_length;
  
  /* we should be full */
  assert(old_dirlen == lhtbl->directory_current);

  new_dirlen = old_dirlen  << 1;

  assert(new_dirlen  < lhtbl->cfg.directory_length_max);

  olddir = lhtbl->directory;

  success = mul_size(new_dirlen, sizeof(segment_t*), &new_dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
  newdir = memcxt->allocate(DIRECTORY, new_dirsz);

  if(newdir == NULL){
    errno = ENOMEM;
    return false;
  }

  /* allocate tries to extend/realloc the old directory */
  if(newdir != olddir){
    for(index = 0; index < old_dirlen; index++){
      newdir[index] = olddir[index];
    }
    lhtbl->directory = newdir;
  }
  
  lhtbl->directory_length = new_dirlen;

  success = mul_size(old_dirlen, sizeof(segment_t*), &old_dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
  memcxt->release(DIRECTORY, olddir, old_dirsz);

  return true;
}


static bool linhash_expand_table(linhash_t* lhtbl){
  size_t seglen;
  size_t new_bindex;
  size_t new_segindex;
  bool success;
  bucket_t** oldbucketp;
  bucket_t* current;
  bucket_t* previous;
  bucket_t* lastofnew;
  segment_t* newseg;
  size_t newsegindex;
  memcxt_t *memcxt;

  memcxt = lhtbl->cfg.memcxt;

  success = add_size(lhtbl->maxp, lhtbl->p, &new_bindex);
  if(!success){
    errno = EINVAL;
    return false;
  }
  
  assert(new_bindex < lhtbl->cfg.bincount_max);

  /* see if the directory needs to grow  */
  if(lhtbl->directory_length  ==  lhtbl->directory_current){
    if(! linhash_expand_directory(lhtbl,  memcxt)){
      return false;
    }
  }


  if(new_bindex < lhtbl->cfg.bincount_max){

    oldbucketp = bindex2bin(lhtbl, lhtbl->p);

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

    assert( newseg->segment[newsegindex] == NULL );

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
  bucket_t* newbucket;
  bucket_t** binp;

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
  bucket_t** binp;
  bucket_t* bucketp;

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
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;

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
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;
  bucket_t* temp_bucketp;

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


size_t bucket_length(bucket_t* bucket){
  size_t count;
  bucket_t* current;

  count = 0;
  current = bucket;
  while(current != NULL){
    count++;
    current = current->next_bucket;
  }

  return count;
}

/*  experimental code below; needs to be cleaned up and debugged */

#if 0
static void linhash_contract_table(linhash_t* lhtbl);

static void linhash_contract_check(linhash_t* lhtbl){
    /* iam Q4: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (lhtbl->count / lhtbl->bincount < lhtbl->cfg.min_load)){
      linhash_contract_table(lhtbl);
      //fprintf(stderr, "TABLE CONTRACTED\n");
    }
}

/* assumes the non-null segments for an prefix of the directory */
static void linhash_contract_directory(linhash_t* lhtbl, memcxt_t* memcxt){
  size_t index;
  size_t oldlen;
  size_t newlen;
  size_t oldsz;
  size_t newsz;
  size_t curlen;
  segment_t** olddir;
  segment_t** newdir;
  bool success;

  oldlen = lhtbl->directory_length;
  curlen = lhtbl->directory_current;
  newlen = oldlen  >> 1;

  success = mul_size(newlen, sizeof(segment_t*), &newsz);

  assert(success);
  
  success = mul_size(oldlen, sizeof(segment_t*), &oldsz);

  assert(success);

  assert(curlen < newlen);
  
  olddir = lhtbl->directory;
  
  newdir = memcxt->allocate(DIRECTORY, newsz);
  
  for(index = 0; index < newlen; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_length = newlen;
  
  memcxt->release(DIRECTORY, olddir, oldsz);
}

static void linhash_contract_table(linhash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  memcxt_t *memcxt;
  size_t srcindex;
  bucket_t** srcbucketp;
  size_t tgtindex;
  bucket_t** tgtbucketp;
  bucket_t* src;
  bucket_t* tgt;
  bool success;
  
  memcxt = lhtbl->cfg.memcxt;

  /* see if the directory needs to contract; iam Q5 need to ensure we don't get unwanted oscillations  load should enter here?!? */
  if((lhtbl->directory_length > lhtbl->cfg.initial_directory_length) && lhtbl->directory_current < lhtbl->directory_length  >> 1){
    //fprintf(stderr, ">DIRECTORY CONTRACTED\n");
    linhash_contract_directory(lhtbl,  memcxt);
    //fprintf(stderr, "<DIRECTORY CONTRACTED\n");
  }

  
  /* get the two buckets involved; moving src to tgt */
  if(lhtbl->p == 0){
    tgtindex = (lhtbl->p >> 1) - 1;
    srcindex = lhtbl->maxp - 1;
  } else {
    tgtindex = lhtbl->p - 1;
    success = add_size(lhtbl->maxp, lhtbl->p - 1, &srcindex);
    assert(success);
  }

  /* 
   * here be the bug: if lhtbl->p = 0 then we cannot just move one
   * bin, we have to move half of them. so we should make sure that
   * moving half keeps the load low.
   */

  
  
  /* update the state variables */
  lhtbl->p -= 1;
  if(lhtbl->p == -1){
    //fprintf(stderr, "STATE CONTRACTED\n");
    lhtbl->maxp = lhtbl->maxp >> 1;
    lhtbl->p = lhtbl->maxp - 1;
    lhtbl->L -= 1;  /* used as a quick test in contraction */
  }


  
  /* get the two buckets involved; moving src to tgt */
  // srcindex = add_size(lhtbl->maxp, lhtbl->p);

  if( srcindex >= lhtbl->bincount ){
    fprintf(stderr, "lhtbl->maxp = %zu\n", lhtbl->maxp);
    fprintf(stderr, "lhtbl->p = %zu\n", lhtbl->p);
    fprintf(stderr, "srcindex = %zu\n", srcindex);
    fprintf(stderr, "bincount = %zu\n", lhtbl->bincount);
  }
  assert( srcindex < lhtbl->bincount);
  
  srcbucketp = bindex2bin(lhtbl, srcindex);

  if(srcbucketp == NULL){
    fprintf(stderr, "srcindex = %zu\n", srcindex);
    fprintf(stderr, "bincount = %zu\n", lhtbl->bincount);
    return;
  }
  
  assert(srcbucketp != NULL);
  
  src = *srcbucketp;
  *srcbucketp = NULL;

  tgtbucketp = bindex2bin(lhtbl, tgtindex);
  tgt = *tgtbucketp;
  
  /* move the buckets */
  if(src != NULL){

    if(tgt == NULL){

      fprintf(stderr, "TARGET BUCKET EMPTY tgtindex = %zu\n", tgtindex);

      /* not very likely */
      *tgtbucketp = src;

    } else {
      /* easiest is to splice the src bin onto the end of the tgt */
      while(src->next_bucket != NULL){  src = src->next_bucket; }
      src->next_bucket = tgt;
    }
  } else {
    fprintf(stderr, "SOURCE BUCKET EMPTY srcindex = %zu\n", srcindex);
  }

  /* now check if we can eliminate a segment */
  seglen = lhtbl->cfg.segment_length;

  segindex = srcindex / seglen;

  if(mod_power_of_two(srcindex, seglen) == 0){
    /* ok we can reclaim it */
    memcxt->release(SEGMENT, lhtbl->directory[segindex], sizeof(segment_t));
    lhtbl->directory[segindex] = NULL;
    lhtbl->directory_current -= 1;
  }
  
  lhtbl->bincount -= 1;
  
}
#endif
