#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "lphash.h"

const bool      lphash_multithreaded             = false;

const size_t    lphash_segment_length            = LPSEGMENT_LENGTH;
const size_t    lphash_initial_directory_length  = LPDIRECTORY_LENGTH;
const size_t    lphash_segments_at_startup       = 1;

const uint16_t  lphash_min_load                 = 2;   
const uint16_t  lphash_max_load                 = 5;

/* toggle for enabling table contraction */
#define LPCONTRACTION_ENABLED  1


/* static routines */
static void lphash_cfg_init(lphash_cfg_t* cfg);

/* lphash expansion routines */
static bool lphash_expand_check(lphash_t* lhtbl);

static bool lphash_expand_directory(lphash_t* lhtbl, lpmemcxt_t* memcxt);

#if LPCONTRACTION_ENABLED
/* lphash contraction routines */

static void lphash_contract_check(lphash_t* lhtbl);

static void lphash_contract_directory(lphash_t* lhtbl, lpmemcxt_t* memcxt);

static void lphash_contract_table(lphash_t* lhtbl);

#endif

/* split's the current bin  */
static bool lphash_expand_table(lphash_t* lhtbl);
 
/* returns the bin index (bindex) of the bin that should contain p  [{ hash }] */
static uint32_t lphash_bindex(lphash_t* lhtbl, const void *p);

/* returns a pointer to the bin i.e. the top bucket at the given bindex  */
static lpbucket_t** lpbindex2bin(lphash_t* lhtbl, uint32_t bindex);

/* returns the length of the linked list starting at the given bucket */
static size_t lpbucket_length(lpbucket_t* bucket);

/* for sanity checking */
static bool lpis_power_of_two(uint32_t n) {
  return (n & (n - 1)) == 0;
}

/* Fast modulo arithmetic, assuming that y is a power of 2 */
static inline size_t lpmod_power_of_two(size_t x, size_t y){
  assert(lpis_power_of_two(y));
  return x & (y - 1);
}

/*
 *  Bruno Dutertre's versions of Jenkin's hash functions.
 *
 */

static uint32_t lpjenkins_hash_uint64(uint64_t x);

static uint32_t lpjenkins_hash_ptr(const void *p);

static void lpinit_pool_memcxt(lpmemcxt_t* pmem);


/* 
 *   Returns the next power of two >= x, maxing
 *   out at 2^31.
 *
 *   http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
 *
 */
static uint32_t lpnext_power_of_two(uint32_t num){
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



/* Add two size_t values, checking for overflow */
static bool lpadd_size(size_t s1, size_t s2, size_t* sum){
  size_t result;
  
  assert(sum != NULL);

  result = s1 + s2;
  if (result < s1 || result < s2){
    return false;
  }

  *sum = result;
  return true;

}


/* Multiply two size_t values, checking for overflow */
static bool lpmul_size(size_t s1, size_t s2, size_t* prod){
  size_t result;

  assert(prod != NULL);

  if (s1 == 0 || s2 == 0){
    *prod = 0;
    return true;
  }
  result = s1 * s2;
  if (result / s2 != s1){
    return false;
  }
  
  *prod = result;
  return result;
}


static inline size_t lphash_load(lphash_t* lhtbl){
  return lhtbl->count / lhtbl->bincount;
}

static void lphash_cfg_init(lphash_cfg_t* cfg){

  lpinit_pool_memcxt(&(cfg->memcxt));

  cfg->multithreaded            = lphash_multithreaded;
  cfg->segment_length           = lphash_segment_length;
  cfg->initial_directory_length = lphash_initial_directory_length;
  cfg->min_load                 = lphash_min_load;   
  cfg->max_load                 = lphash_max_load;
  cfg->bincount_max             = UINT32_MAX;
  cfg->directory_length_max     = cfg->bincount_max / LPSEGMENT_LENGTH;

  assert(lpis_power_of_two(cfg->segment_length));
  assert(lpis_power_of_two(cfg->initial_directory_length));

  /* in non debug mode we do our best... */

  if(!lpis_power_of_two(cfg->segment_length)){
    cfg->segment_length = lpnext_power_of_two(cfg->segment_length);
  }
  
  if(!lpis_power_of_two(cfg->initial_directory_length)){
    cfg->initial_directory_length = lpnext_power_of_two(cfg->initial_directory_length);
  }
  
  
}


/* API */
bool init_lphash(lphash_t* lhtbl){
  lphash_cfg_t* lhtbl_cfg;
  size_t index;
  lpsegment_t* seg;
  bool success;
  size_t dirsz;
  size_t binsz;
  lpmemcxt_t* memcxt;
  
  if(lhtbl == NULL){
    errno = EINVAL;
    return false;
  }
  
  assert(lhtbl != NULL);

  lhtbl_cfg = &lhtbl->cfg;
  
  lphash_cfg_init(lhtbl_cfg);
    
  memcxt = &(lhtbl_cfg->memcxt);
    
  /* lock for resolving contention  (only when cfg->multithreaded)   */
  if(lhtbl_cfg->multithreaded){
    pthread_mutex_init(&lhtbl->mutex, NULL);
  }

  lhtbl->directory_length = lhtbl_cfg->initial_directory_length;
  lhtbl->directory_current = lphash_segments_at_startup; 

  
  /* the size of the directory */
  success = lpmul_size(lhtbl->directory_length, sizeof(lpsegment_t*), &dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
    
  /* the directory; i.e. the array of segment pointers */
  lhtbl->directory = memcxt->allocate(LPDIRECTORY, dirsz);
  if(lhtbl->directory == NULL){
    errno = ENOMEM;
    return false;
  }
  
  /* mininum number of bins    [{ N }]   */
  success = lpmul_size(lhtbl_cfg->segment_length, lhtbl->directory_current, &binsz);
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

  assert(lpis_power_of_two(lhtbl->maxp));

  /* create the segments needed by the current directory */
  for(index = 0; index < lhtbl->directory_current; index++){
    seg = (lpsegment_t*)memcxt->allocate(LPSEGMENT, sizeof(lpsegment_t));
    lhtbl->directory[index] = seg;
    if(seg == NULL){
      errno = ENOMEM;
      return false;
    }
  }

  return true;
}

/* API */
void dump_lphash(FILE* fp, lphash_t* lhtbl, bool showloads){
  size_t index;
  size_t blen;
  lpbucket_t** bp;

  size_t maxlength;
  
  fprintf(fp, "directory_length = %" PRIuPTR "\n", lhtbl->directory_length);
  fprintf(fp, "directory_current = %" PRIuPTR "\n", lhtbl->directory_current);
  fprintf(fp, "N = %" PRIuPTR "\n", lhtbl->N);
  fprintf(fp, "L = %" PRIuPTR "\n", lhtbl->L);
  fprintf(fp, "count = %" PRIuPTR "\n", lhtbl->count);
  fprintf(fp, "maxp = %" PRIuPTR "\n", lhtbl->maxp);
  fprintf(fp, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
  fprintf(fp, "load = %" PRIuPTR "\n", lphash_load(lhtbl));
  
  maxlength = 0;
  
  if(showloads){
    fprintf(fp, "bucket lengths: ");
  }

  for(index = 0; index < lhtbl->bincount; index++){
    bp = lpbindex2bin(lhtbl, index);
    assert(bp != NULL);
    blen = lpbucket_length(*bp);
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


/* API */
void delete_lphash(lphash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  size_t index;
  lpsegment_t* current_segment;
  lpbucket_t* current_bucket;
  lpbucket_t* next_bucket;
  lpmemcxt_t *memcxt;
  size_t dirsz;
  bool success;

  seglen = lhtbl->cfg.segment_length;
  memcxt = &(lhtbl->cfg.memcxt);

  for(segindex = 0; segindex < lhtbl->directory_current; segindex++){

    current_segment = lhtbl->directory[segindex];
    
    /* cdr down the segment and release the linked list of buckets */
      for(index = 0; index < seglen; index++){
	current_bucket = current_segment->segment[index];

	while(current_bucket != NULL){

	  next_bucket = current_bucket->next_bucket;
	  memcxt->release(LPBUCKET, current_bucket, sizeof(lpbucket_t));
	  current_bucket = next_bucket;
	}
      }
      /* now release the segment */
      memcxt->release(LPSEGMENT, current_segment,  sizeof(lpsegment_t));
  }
  
  success = lpmul_size(lhtbl->directory_length, sizeof(lpsegment_t*), &dirsz);
  assert(success);
  if(success){
    memcxt->release(LPDIRECTORY, lhtbl->directory, dirsz);
  }
}


/* returns the raw bindex/index of the bin that should contain p  [{ hash }] */
static uint32_t lphash_bindex(lphash_t* lhtbl, const void *p){
  uint32_t jhash;
  uint32_t l;
  size_t next_maxp;

  
  jhash  = lpjenkins_hash_ptr(p);

  l = lpmod_power_of_two(jhash, lhtbl->maxp);

  if(l < lhtbl->p){

    next_maxp = lhtbl->maxp << 1;

    /* if we can expand to next_maxp we use that */
    if(next_maxp < lhtbl->cfg.bincount_max){
      l = lpmod_power_of_two(jhash, next_maxp);
    }
    
  }
  
  return l;
}

static lpbucket_t** lpbindex2bin(lphash_t* lhtbl, uint32_t bindex){
  lpsegment_t* segptr;
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

  index = lpmod_power_of_two(bindex, seglen);

  return &(segptr->segment[index]);
}


/* 
 *  pointer (in the appropriate segment) to the lpbucket_t* 
 *  where the start of the bucket chain should be for p 
 */
static lpbucket_t** lphash_fetch_bucket(lphash_t* lhtbl, const void *p){
  uint32_t bindex;
  
  bindex = lphash_bindex(lhtbl, p);

  return lpbindex2bin(lhtbl, bindex);
}

/* check if the table needs to be expanded; true if either it didn't need to be 
 * expanded, or else it expanded successfully. false if it failed.
 *
 */
static bool lphash_expand_check(lphash_t* lhtbl){
  if((lhtbl->bincount < lhtbl->cfg.bincount_max) && (lphash_load(lhtbl) > lhtbl->cfg.max_load)){
    return lphash_expand_table(lhtbl);
  }
  return true;
}


static bool lphash_expand_directory(lphash_t* lhtbl, lpmemcxt_t* memcxt){
  size_t index;
  size_t old_dirlen;
  size_t old_dirsz;
  size_t new_dirlen;
  size_t new_dirsz;
  bool success;
  lpsegment_t** olddir;
  lpsegment_t** newdir;

  old_dirlen = lhtbl->directory_length;
  
  /* we should be full */
  assert(old_dirlen == lhtbl->directory_current);

  new_dirlen = old_dirlen  << 1;

  assert(new_dirlen  < lhtbl->cfg.directory_length_max);

  olddir = lhtbl->directory;

  success = lpmul_size(new_dirlen, sizeof(lpsegment_t*), &new_dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
  newdir = memcxt->allocate(LPDIRECTORY, new_dirsz);

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

  success = lpmul_size(old_dirlen, sizeof(lpsegment_t*), &old_dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
  memcxt->release(LPDIRECTORY, olddir, old_dirsz);

  return true;
}


static bool lphash_expand_table(lphash_t* lhtbl){
  size_t seglen;
  size_t new_bindex;
  size_t new_segindex;
  bool success;
  lpbucket_t** oldbucketp;
  lpbucket_t* current;
  lpbucket_t* previous;
  lpbucket_t* lastofnew;
  lpsegment_t* newseg;
  size_t newsegindex;
  lpmemcxt_t *memcxt;

  memcxt = &(lhtbl->cfg.memcxt);

  success = lpadd_size(lhtbl->maxp, lhtbl->p, &new_bindex);
  if(!success){
    errno = EINVAL;
    return false;
  }
  
  assert(new_bindex < lhtbl->cfg.bincount_max);

  /* see if the directory needs to grow  */
  if(lhtbl->directory_length  ==  lhtbl->directory_current){
    if(! lphash_expand_directory(lhtbl,  memcxt)){
      return false;
    }
  }


  if(new_bindex < lhtbl->cfg.bincount_max){

    oldbucketp = lpbindex2bin(lhtbl, lhtbl->p);

    seglen = lhtbl->cfg.segment_length;

    new_segindex = new_bindex / seglen;

    newsegindex = lpmod_power_of_two(new_bindex, seglen);  
    
    /* expand address space; if necessary create new segment */  
    if((newsegindex == 0) && (lhtbl->directory[new_segindex] == NULL)){
      newseg = memcxt->allocate(LPSEGMENT, sizeof(lpsegment_t));
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

      if(lphash_bindex(lhtbl, current->key) == new_bindex){

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




/* API */
bool lphash_insert(lphash_t* lhtbl, const void *key, const void *value){
  lpbucket_t* newbucket;
  lpbucket_t** binp;
  lpmemcxt_t *memcxt;

  memcxt = &(lhtbl->cfg.memcxt);
  
  binp = lphash_fetch_bucket(lhtbl, key);

  newbucket = memcxt->allocate(LPBUCKET, sizeof(lpbucket_t));
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
  return lphash_expand_check(lhtbl);
}

/* API */
void *lphash_lookup(lphash_t* lhtbl, const void *key){
  void* value;
  lpbucket_t** binp;
  lpbucket_t* bucketp;

  value = NULL;
  binp = lphash_fetch_bucket(lhtbl, key);
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

/* API */
bool lphash_delete(lphash_t* lhtbl, const void *key){
  bool found = false;
  lpbucket_t** binp;
  lpbucket_t* current_bucketp;
  lpbucket_t* previous_bucketp;
  lpmemcxt_t *memcxt;

  memcxt = &(lhtbl->cfg.memcxt);


  previous_bucketp = NULL;
  binp = lphash_fetch_bucket(lhtbl, key);
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    if(key == current_bucketp->key){
      found = true;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      memcxt->release(LPBUCKET, current_bucketp, sizeof(lpbucket_t));

      /* census adjustments */
      lhtbl->count--;

      break;
    }
    previous_bucketp = current_bucketp;
    current_bucketp = current_bucketp->next_bucket;
  }

#if LPCONTRACTION_ENABLED
  /* should we contract */
  if(found){
    lphash_contract_check(lhtbl);
  }
#endif
  
  return found;
}

/* API */
size_t lphash_delete_all(lphash_t* lhtbl, const void *key){
  size_t count;
  lpbucket_t** binp;
  lpbucket_t* current_bucketp;
  lpbucket_t* previous_bucketp;
  lpbucket_t* temp_bucketp;
  lpmemcxt_t *memcxt;

  memcxt = &(lhtbl->cfg.memcxt);

  count = 0;
  previous_bucketp = NULL;
  binp = lphash_fetch_bucket(lhtbl, key);
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
      memcxt->release(LPBUCKET, temp_bucketp, sizeof(lpbucket_t));
    } else {
      previous_bucketp = current_bucketp;
      current_bucketp = current_bucketp->next_bucket;
    }
  }

  /* census adjustments */
  lhtbl->count -= count;

#if LPCONTRACTION_ENABLED
  /* should we contract */
  if(count > 0){
    lphash_contract_check(lhtbl);
  }
#endif

  return count;
}


static size_t lpbucket_length(lpbucket_t* bucket){
  size_t count;
  lpbucket_t* current;

  count = 0;
  current = bucket;
  while(current != NULL){
    count++;
    current = current->next_bucket;
  }

  return count;
}



#if LPCONTRACTION_ENABLED

static void lphash_contract_check(lphash_t* lhtbl){
    /* iam: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (lphash_load(lhtbl) < lhtbl->cfg.min_load)){
      lphash_contract_table(lhtbl);
    }
}

/* assumes the non-null segments for an prefix of the directory */
static void lphash_contract_directory(lphash_t* lhtbl, lpmemcxt_t* memcxt){
  size_t index;
  size_t oldlen;
  size_t newlen;
  size_t oldsz;
  size_t newsz;
  size_t curlen;
  lpsegment_t** olddir;
  lpsegment_t** newdir;
  bool success;

  oldlen = lhtbl->directory_length;
  curlen = lhtbl->directory_current;
  newlen = oldlen  >> 1;

  success = lpmul_size(newlen, sizeof(lpsegment_t*), &newsz);

  assert(success);
  if(! success ){
    return;
  }
  
  success = lpmul_size(oldlen, sizeof(lpsegment_t*), &oldsz);

  assert(success);
  if(! success ){
    return;
  }

  assert(curlen < newlen);
  if(curlen >= newlen){
    return;
  }
  
  olddir = lhtbl->directory;
  
  newdir = memcxt->allocate(LPDIRECTORY, newsz);
  
  for(index = 0; index < newlen; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_length = newlen;
  
  memcxt->release(LPDIRECTORY, olddir, oldsz);
}

static inline void check_index(size_t index, const char* name, lphash_t* lhtbl){
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
static inline void move_buckets(lpbucket_t** srcbin, lpbucket_t** tgtbin){
  lpbucket_t* src;
  lpbucket_t* tgt;
  lpbucket_t* tmp;

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

static void lphash_contract_table(lphash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  lpmemcxt_t *memcxt;
  size_t srcindex;
  lpbucket_t** srcbin;
  size_t tgtindex;
  lpbucket_t** tgtbin;
  bool success;

  memcxt = &(lhtbl->cfg.memcxt);

  /* 
     see if the directory needs to contract; 
     iam: need to ensure we don't get unwanted oscillations;
     should load should enter here?!? 
  */
  if((lhtbl->directory_length > lhtbl->cfg.initial_directory_length) &&
     (lhtbl->directory_current < lhtbl->directory_length  >> 1)){
    lphash_contract_directory(lhtbl,  memcxt);
  }

  
  /* get the two buckets involved; moving src to tgt */
  if(lhtbl->p == 0){
    tgtindex = (lhtbl->maxp >> 1) - 1;
    srcindex = lhtbl->maxp - 1;
  } else {
    tgtindex = lhtbl->p - 1;
    success = lpadd_size(lhtbl->maxp, lhtbl->p - 1, &srcindex);
    assert(success);
    if( ! success ){
      return;
    }
  }

  check_index(srcindex, "src",  lhtbl);

  check_index(tgtindex, "tgt",  lhtbl);

  /* get the two buckets involved; moving src to tgt */
  
  srcbin = lpbindex2bin(lhtbl, srcindex);

  tgtbin = lpbindex2bin(lhtbl, tgtindex);

  /* move the buckets */
  move_buckets(srcbin, tgtbin);

  
  /* now check if we can eliminate a segment */
  seglen = lhtbl->cfg.segment_length;

  segindex = srcindex / seglen;

  if(lpmod_power_of_two(srcindex, seglen) == 0){
    /* ok we can reclaim it */
    memcxt->release(LPSEGMENT, lhtbl->directory[segindex], sizeof(lpsegment_t));
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
static uint32_t lpjenkins_hash_uint64(uint64_t x) {
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
static uint32_t lpjenkins_hash_ptr(const void *p) {
  return lpjenkins_hash_uint64((uint64_t) ((size_t) p));
}




#define BP_SCALE  32
/* one thing for every bit in the bitmask */
#define BP_LENGTH  BP_SCALE * 64  

#define SP_SCALE  8
/* one thing for every bit in the bitmask */
#define SP_LENGTH SP_SCALE * 64  

/* lpbucket_pool_t is defined in types.h */
struct lpbucket_pool_s {
  lpbucket_t pool[BP_LENGTH];       /* the pool of buckets; one for each bit in the bitmask array */
  uint64_t bitmasks[BP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use */
  size_t free_count;              /* the current count of free buckets in this pool             */
  void* next_bucket_pool;         /* the next bucket pool to look if this one is full           */
};

/* lpsegment_pool_t is defined in types.h */
struct lpsegment_pool_s {
  lpsegment_t pool[SP_LENGTH];      /* the pool of segments; one for each bit in the bitmask array */
  uint64_t bitmasks[SP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use  */
  size_t free_count;              /* the current count of free segments in this pool             */
  void* next_segment_pool;        /* the next segment pool to look if this one is full           */
};


//DD: is there a one-one or a many-to-one relationship b/w linhash tables and pools?
typedef struct lppool_s {
  void *directory;              /* not sure if this is needed/desired */
  lpsegment_pool_t* segments;
  lpbucket_pool_t* buckets;
} lppool_t;


#ifndef NDEBUG
static bool lpsane_bucket_pool(lpbucket_pool_t* bpool);
#endif

/* for now we do not assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_lpbucket_pool(lpbucket_pool_t* bp){
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

  assert(lpsane_bucket_pool(bp));
}

/* for now we  do not assume assume that the underlying memory has been mmapped (i.e zeroed) */
static void init_lpsegment_pool(lpsegment_pool_t* sp){
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

static void* lppool_mmap(void* oldaddr, size_t size){
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

static bool lppool_munmap(void* memory, size_t size){
  int rcode;

  rcode = munmap(memory, size);
  
  return rcode != -1;
}


static void* new_lpdirectory(lppool_t* pool, size_t size){
  pool->directory = lppool_mmap(pool->directory, size);
  return pool->directory;
}

static lpsegment_pool_t* new_lpsegments(void){
  lpsegment_pool_t* sptr;
  sptr = lppool_mmap(NULL, sizeof(lpsegment_pool_t));
  if(sptr == NULL){
    return NULL;
  }
  init_lpsegment_pool(sptr);
  return sptr;
}

static void* new_lpbuckets(void){
  lpbucket_pool_t* bptr;
  bptr = lppool_mmap(NULL, sizeof(lpbucket_pool_t));
  if(bptr == NULL){
    return NULL;
  }
  init_lpbucket_pool(bptr);
  return bptr;
}

#ifndef NDEBUG
#if 0
static bool lpsane_bucket_pool(lpbucket_pool_t* bpool){
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
    fprintf(stderr, "lpsane_bucket_pool: free_count = %zu bit_free_count = %zu\n", free_count, bit_free_count);
    for(scale = 0; scale < BP_SCALE; scale++){
      fprintf(stderr, "\tbpool_current->bitmasks[%zu] = %"PRIu64"\n", scale, bpool->bitmasks[scale]);
    }
    return false;
  }

  for(bindex = 0; bindex < BP_LENGTH; bindex++){
    if(bpool->pool[bindex].bucket_pool_ptr != bpool){
      fprintf(stderr, "lpsane_bucket_pool: bucket_pool_ptr = %p not correct:  lpbucket_pool_t* %p\n",
	      bpool->pool[bindex].bucket_pool_ptr, bpool);
    }
  }

  return true;
}
#else
static inline bool lpsane_bucket_pool(lpbucket_pool_t *bpool) {
  return true;
}
#endif
#endif


static void lpinit_pool(lppool_t* pool){
  pool->directory = new_lpdirectory(pool, LPDIRECTORY_LENGTH * sizeof(void*));
  pool->segments = new_lpsegments();
  pool->buckets = new_lpbuckets();
}


// courtesy of BD 
#ifdef __GNUC__

static inline uint32_t lpctz64(uint64_t x) {
  assert(x != 0);
  return __builtin_ctzl(x);
}

#else

static inline uint32_t lpctz64(uint64_t x) {
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
static uint32_t get_free_lpbit(uint64_t mask){
  uint32_t index;
  uint64_t flipped;
  assert(mask != UINT64_MAX);
  flipped = ~mask;
  index = lpctz64(flipped);  
  return index;
}

#ifndef NDEBUG
static inline bool get_lpbit(uint64_t mask, uint32_t index) {
  assert((0 <= index) && (index < 64));
  return (mask & (((uint64_t)1) << index)) ? true : false;
}
#endif

/* sets the bit specified by the index in the mask */
static inline uint64_t set_lpbit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < 64);
  return mask | (((uint64_t)1) << index);
}

/* clear the bit */
static inline uint64_t clear_lpbit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < 64);
  return mask & ~(((uint64_t)1) << index); 
}

static lpbucket_t* alloc_lpbucket(lppool_t* pool){
  lpbucket_t *buckp;
  lpbucket_pool_t* bpool_current;
  size_t scale;
  size_t index;
  
  buckp = NULL;

  // BD: use a for loop
  for (bpool_current = pool->buckets; bpool_current != NULL; bpool_current = bpool_current->next_bucket_pool) {
    assert(lpsane_bucket_pool(bpool_current));

    if (bpool_current->free_count > 0) {

      /* lets go through the blocks looking for a free bucket */
      for(scale = 0; scale < BP_SCALE; scale++) {
	if(bpool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_lpbit(bpool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));
	  buckp = &bpool_current->pool[(scale * 64) + index];
	  bpool_current->bitmasks[scale] = set_lpbit(bpool_current->bitmasks[scale], index);
	  bpool_current->free_count --;

	  assert(lpsane_bucket_pool(bpool_current));
	  assert(buckp != NULL);

	  return buckp;
	}
      }
    }
  }

  assert(buckp == NULL);
  assert(bpool_current  == NULL);

  /* need to allocate another bpool */
  bpool_current = new_lpbuckets();
  if (bpool_current != NULL) {
    /* put the new bucket up front */
    bpool_current->next_bucket_pool = pool->buckets;
    pool->buckets = bpool_current;
    
    /* return the first bucket of the new pool */
    buckp = &bpool_current->pool[0];
    bpool_current->free_count --;
    bpool_current->bitmasks[0] = 1; // low-order bit is set
  
    assert(lpsane_bucket_pool(bpool_current));
    assert(buckp != NULL);
  }

  return buckp;
}

static bool free_lpbucket(lppool_t* pool, lpbucket_t* buckp){
  lpbucket_pool_t* bpool;
  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(pool != NULL);
  assert(buckp != NULL);

  /* get the bucket pool that we belong to */
  bpool = buckp->bucket_pool_ptr;

  /* sanity check */
  assert((bpool->pool <= buckp) && (buckp < bpool->pool + BP_LENGTH));
  assert(lpsane_bucket_pool(bpool));

  index = buckp - bpool->pool;

  pmask_index = index / 64;
  pmask_bit = index % 64;

  assert(get_lpbit(bpool->bitmasks[pmask_index], pmask_bit)); 

  bpool->bitmasks[pmask_index] = clear_lpbit(bpool->bitmasks[pmask_index], pmask_bit);
  bpool->free_count ++;
  
  assert((bpool->free_count > 0) && (bpool->free_count <= BP_LENGTH));
  assert(lpsane_bucket_pool(bpool));
  
  return true;
}


static lpsegment_t* alloc_lpsegment(lppool_t* pool){
  lpsegment_t *segp;
  lpsegment_pool_t* spool_current;
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
	  index = get_free_lpbit(spool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));

	  /* ok we can use this one */
	  segp = &spool_current->pool[(scale * 64) + index];
	  spool_current->bitmasks[scale] = set_lpbit(spool_current->bitmasks[scale], index);
	  spool_current->free_count --;

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
    
    spool_current = new_lpsegments();
    if(spool_current == NULL){
      return NULL;
    }

    /* put the new segment up front */
    spool_current->next_segment_pool = pool->segments;
    pool->segments = spool_current;

    /* return the first segment in the new pool */
    segp = &spool_current->pool[0];
    spool_current->free_count --;
    spool_current->bitmasks[0] = 1; // set low-order bit
  } 

  return segp;
}

static bool free_lpsegment(lppool_t* pool, lpsegment_t* segp){
  lpsegment_pool_t* spool;

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

  assert(get_lpbit(spool->bitmasks[pmask_index], pmask_bit));
  spool->bitmasks[pmask_index] = clear_lpbit(spool->bitmasks[pmask_index], pmask_bit); 
  spool->free_count ++;

  assert((spool->free_count > 0) && (spool->free_count <= SP_LENGTH));
  
  return true;
}

/* just one for now */
static bool the_lppool_is_ok = false;
static lppool_t the_pool;

static void check_lppool(void){
  if(!the_lppool_is_ok){
    lpinit_pool(&the_pool);
    the_lppool_is_ok = true;
  }
}

static void *lppool_allocate(lppool_t* pool, lpmemtype_t type, size_t size);

static void lppool_release(lppool_t* pool, lpmemtype_t type, void *ptr, size_t size);

static void *_lppool_allocate(lpmemtype_t type, size_t size);

static void _lppool_release(lpmemtype_t type, void *ptr, size_t size);


static void lpinit_pool_memcxt(lpmemcxt_t* pmem){
  if(pmem != NULL){
    pmem->allocate =  _lppool_allocate;
    pmem->release = _lppool_release;
  }
}

static void *_lppool_allocate(lpmemtype_t type, size_t size){
  return lppool_allocate(&the_pool, type, size);
}

static void _lppool_release(lpmemtype_t type, void *ptr, size_t size){
  lppool_release(&the_pool, type, ptr, size);
}



static void *lppool_allocate(lppool_t* pool, lpmemtype_t type, size_t size){
  void *memory;
  check_lppool();
  switch(type){
  case LPDIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    // how many hash tables use this pool?
    memory = new_lpdirectory(pool, size);
    break;
  }
  case LPSEGMENT: {
    memory = alloc_lpsegment(pool);
    break;
  }
  case LPBUCKET: {
    memory = alloc_lpbucket(pool);
    break;
  }
  default: assert(false);
    memory = NULL;
  }
  return memory;
}


static void lppool_release(lppool_t* pool, lpmemtype_t type, void *ptr, size_t size){
  check_lppool();
  switch(type){
  case LPDIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    lppool_munmap(ptr, size);

    break;
  }
  case LPSEGMENT: {
    free_lpsegment(pool, ptr);
    break;
  }
  case LPBUCKET: {
    free_lpbucket(pool, ptr);
    break;
  }
  default: assert(false);
  }
  
}

#if 0
static void dump_lppool(FILE* fp){
  float bp = sizeof(lpbucket_pool_t);
  float sp = sizeof(lpsegment_pool_t);
  bp /= 4096;
  sp /= 4096;
  fprintf(fp, "sizeof(lpbucket_pool_t) =  %zu\n", sizeof(lpbucket_pool_t));
  fprintf(fp, "pages: %f\n", bp);
  fprintf(fp, "sizeof(lpsegment_pool_t) =  %zu\n", sizeof(lpsegment_pool_t));
  fprintf(fp, "pages: %f\n", sp);

}
#endif
