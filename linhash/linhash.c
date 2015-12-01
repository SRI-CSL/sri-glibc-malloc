#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>

#include "linhash.h"
#include "hashfns.h"
#include "memcxt.h"


const bool      linhash_multithreaded             = false;

const size_t    linhash_segment_length            = SEGMENT_LENGTH;
const size_t    linhash_initial_directory_length  = DIRECTORY_LENGTH;
const size_t    linhash_segments_at_startup       = 1;

const uint16_t  linhash_min_load                 = 2;   
const uint16_t  linhash_max_load                 = 5;

/* toggle for enabling table contraction */
#define CONTRACTION_ENABLED  1


/* static routines */
static void linhash_cfg_init(linhash_cfg_t* cfg, memcxt_t* memcxt);

/* linhash expansion routines */
static bool linhash_expand_check(linhash_t* lhtbl);

static bool linhash_expand_directory(linhash_t* lhtbl, memcxt_t* memcxt);

#if CONTRACTION_ENABLED
/* linhash contraction routines */

static void linhash_contract_check(linhash_t* lhtbl);

static void linhash_contract_directory(linhash_t* lhtbl, memcxt_t* memcxt);

static void linhash_contract_table(linhash_t* lhtbl);

#endif

/* split's the current bin  */
static bool linhash_expand_table(linhash_t* lhtbl);
 
/* returns the bin index (bindex) of the bin that should contain p  [{ hash }] */
static uint32_t linhash_bindex(linhash_t* lhtbl, const void *p);

/* returns a pointer to the bin i.e. the top bucket at the given bindex  */
static bucket_t** bindex2bin(linhash_t* lhtbl, uint32_t bindex);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucket_t* bucket);

/* for sanity checking */
static bool is_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}

/* Fast modulo arithmetic, assuming that y is a power of 2 */
static inline size_t mod_power_of_two(size_t x, size_t y){
  assert(is_power_of_two(y));
  return x & (y - 1);
}

/* 
 *   Returns the next power of two >= x, maxing
 *   out at 2^31.
 *
 *   http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
 *
 */
static uint32_t next_power_of_two(uint32_t num){
  uint32_t val;
  if(num == 0){
    val = 2;
  } else if(num > (((uint32_t)1) << 31)){
    val = (((uint32_t)1) << 31);
  } else {
    val = num;
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;
  }
  return val;
}


static inline size_t linhash_load(linhash_t* lhtbl){
  return lhtbl->count / lhtbl->bincount;
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

  assert(is_power_of_two(cfg->segment_length));
  assert(is_power_of_two(cfg->initial_directory_length));

  /* in non debug mode we do our best... */

  if(!is_power_of_two(cfg->segment_length)){
    cfg->segment_length = next_power_of_two(cfg->segment_length);
  }
  
  if(!is_power_of_two(cfg->initial_directory_length)){
    cfg->initial_directory_length = next_power_of_two(cfg->initial_directory_length);
  }
  
  
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
  
  fprintf(fp, "directory_length = %" PRIuPTR "\n", lhtbl->directory_length);
  fprintf(fp, "directory_current = %" PRIuPTR "\n", lhtbl->directory_current);
  fprintf(fp, "N = %" PRIuPTR "\n", lhtbl->N);
  fprintf(fp, "L = %" PRIuPTR "\n", lhtbl->L);
  fprintf(fp, "count = %" PRIuPTR "\n", lhtbl->count);
  fprintf(fp, "maxp = %" PRIuPTR "\n", lhtbl->maxp);
  fprintf(fp, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
  fprintf(fp, "load = %" PRIuPTR "\n", linhash_load(lhtbl));
  
  maxlength = 0;
  
  if(showloads){
    fprintf(fp, "bucket lengths: ");
  }

  for(index = 0; index < lhtbl->bincount; index++){
    bp = bindex2bin(lhtbl, index);
    assert(bp != NULL);
    blen = bucket_length(*bp);
    if( blen > maxlength ){  maxlength = blen; }
    if(showloads && blen != 0){
      fprintf(fp, "%" PRIuPTR ":%" PRIuPTR " ", index, blen);
    }
  }
  if(showloads){
    fprintf(fp, "\n");
  }
  fprintf(fp, "maximum length = %" PRIuPTR "\n\n", maxlength);
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
  if( ! ( bindex < lhtbl->bincount ) ){
    return NULL;
  }
  
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
  if((lhtbl->bincount < lhtbl->cfg.bincount_max) && (linhash_load(lhtbl) > lhtbl->cfg.max_load)){
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
	if( lastofnew == NULL ){     
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

#if CONTRACTION_ENABLED
  /* should we contract */
  if(found){
    linhash_contract_check(lhtbl);
  }
#endif
  
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

#if CONTRACTION_ENABLED
  /* should we contract */
  if(count > 0){
    linhash_contract_check(lhtbl);
  }
#endif

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



#if CONTRACTION_ENABLED

static void linhash_contract_check(linhash_t* lhtbl){
    /* iam: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (linhash_load(lhtbl) < lhtbl->cfg.min_load)){
      linhash_contract_table(lhtbl);
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

static inline void check_index(size_t index, const char* name, linhash_t* lhtbl){
#ifndef NDEBUG
  if( index >= lhtbl->bincount ){
    fprintf(stderr, "%s index = %" PRIuPTR "\n", name, index);
    fprintf(stderr, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
    fprintf(stderr, "lhtbl->maxp = %" PRIuPTR "\n", lhtbl->maxp);
    fprintf(stderr, "lhtbl->p = %" PRIuPTR "\n", lhtbl->p);
  }
  assert( index < lhtbl->bincount);
#endif
}

/* move all the buckets in the src bin to the tgt bin */
static inline void move_buckets(bucket_t** srcbin, bucket_t** tgtbin){
  bucket_t* src;
  bucket_t* tgt;
  bucket_t* tmp;

  assert(srcbin != NULL);
  assert(tgtbin != NULL);
  
  src = *srcbin;
  tgt = *tgtbin;

  /* move the buckets */
  if(src != NULL){

    if(tgt == NULL){

      /* not very likely */
      *tgtbin = src;
      *srcbin = NULL;
      
    } else {
      /* easiest is to splice the src bin onto the end of the tgt */
      tmp = tgt;
      while(tmp->next_bucket != NULL){  tmp = tmp->next_bucket; }
      tmp->next_bucket = src;
      *srcbin = NULL;
    }
  } 
}

static void linhash_contract_table(linhash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  memcxt_t *memcxt;
  size_t srcindex;
  bucket_t** srcbin;
  size_t tgtindex;
  bucket_t** tgtbin;
  bool success;

  memcxt = lhtbl->cfg.memcxt;

  /* 
     see if the directory needs to contract; 
     iam: need to ensure we don't get unwanted oscillations;
     should load should enter here?!? 
  */
  if((lhtbl->directory_length > lhtbl->cfg.initial_directory_length) &&
     (lhtbl->directory_current < lhtbl->directory_length  >> 1)){
    linhash_contract_directory(lhtbl,  memcxt);
  }

  
  /* get the two buckets involved; moving src to tgt */
  if(lhtbl->p == 0){
    tgtindex = (lhtbl->maxp >> 1) - 1;
    srcindex = lhtbl->maxp - 1;
  } else {
    tgtindex = lhtbl->p - 1;
    success = add_size(lhtbl->maxp, lhtbl->p - 1, &srcindex);
    assert(success);
  }

  check_index(srcindex, "src",  lhtbl);

  check_index(tgtindex, "tgt",  lhtbl);

  /* get the two buckets involved; moving src to tgt */
  
  srcbin = bindex2bin(lhtbl, srcindex);

  tgtbin = bindex2bin(lhtbl, tgtindex);

  /* move the buckets */
  move_buckets(srcbin, tgtbin);

  
  /* now check if we can eliminate a segment */
  seglen = lhtbl->cfg.segment_length;

  segindex = srcindex / seglen;

  if(mod_power_of_two(srcindex, seglen) == 0){
    /* ok we can reclaim it */
    memcxt->release(SEGMENT, lhtbl->directory[segindex], sizeof(segment_t));
    lhtbl->directory[segindex] = NULL;
    lhtbl->directory_current -= 1;
  }
  
  /* update the state variables */
  lhtbl->p -= 1;
  if(lhtbl->p == -1){
    lhtbl->maxp = lhtbl->maxp >> 1;
    lhtbl->p = lhtbl->maxp - 1;
    lhtbl->L -= 1;  /* used as a quick test in contraction */
  }


  lhtbl->bincount -= 1;
  
}


#endif

