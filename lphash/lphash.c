#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "lphash.h"


const bool      lphash_multithreaded             = false;

const size_t    lphash_segment_length            = SEGMENT_LENGTH;
const size_t    lphash_initial_directory_length  = DIRECTORY_LENGTH;
const size_t    lphash_segments_at_startup       = 1;

const uint16_t  lphash_min_load                 = 2;   
const uint16_t  lphash_max_load                 = 5;

/* toggle for enabling table contraction */
#define CONTRACTION_ENABLED  1


/* static routines */
static void lphash_cfg_init(lphash_cfg_t* cfg);

/* lphash expansion routines */
static bool lphash_expand_check(lphash_t* lhtbl);

static bool lphash_expand_directory(lphash_t* lhtbl, memcxt_t* memcxt);

#if CONTRACTION_ENABLED
/* lphash contraction routines */

static void lphash_contract_check(lphash_t* lhtbl);

static void lphash_contract_directory(lphash_t* lhtbl, memcxt_t* memcxt);

static void lphash_contract_table(lphash_t* lhtbl);

#endif

/* split's the current bin  */
static bool lphash_expand_table(lphash_t* lhtbl);
 
/* returns the bin index (bindex) of the bin that should contain p  [{ hash }] */
static uint32_t lphash_bindex(lphash_t* lhtbl, const void *p);

/* returns a pointer to the bin i.e. the top bucket at the given bindex  */
static bucket_t** bindex2bin(lphash_t* lhtbl, uint32_t bindex);

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
 *  Bruno Dutertre's versions of Jenkin's hash functions.
 *
 */

static uint32_t jenkins_hash_uint64(uint64_t x);

static uint32_t jenkins_hash_ptr(const void *p);

static void init_pool_memcxt(memcxt_t* pmem);


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



/* Add two size_t values, checking for overflow */
static bool add_size(size_t s1, size_t s2, size_t* sum){
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
static bool mul_size(size_t s1, size_t s2, size_t* prod){
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

  init_pool_memcxt(&(cfg->memcxt));

  cfg->multithreaded            = lphash_multithreaded;
  cfg->segment_length           = lphash_segment_length;
  cfg->initial_directory_length = lphash_initial_directory_length;
  cfg->min_load                 = lphash_min_load;   
  cfg->max_load                 = lphash_max_load;
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



bool init_lphash(lphash_t* lhtbl){
  lphash_cfg_t* lhtbl_cfg;
  size_t index;
  segment_t* seg;
  bool success;
  size_t dirsz;
  size_t binsz;
  memcxt_t* memcxt;
  
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

void dump_lphash(FILE* fp, lphash_t* lhtbl, bool showloads){
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
  fprintf(fp, "load = %" PRIuPTR "\n", lphash_load(lhtbl));
  
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


void delete_lphash(lphash_t* lhtbl){
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
  memcxt = &(lhtbl->cfg.memcxt);

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
static uint32_t lphash_bindex(lphash_t* lhtbl, const void *p){
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

bucket_t** bindex2bin(lphash_t* lhtbl, uint32_t bindex){
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
bucket_t** lphash_fetch_bucket(lphash_t* lhtbl, const void *p){
  uint32_t bindex;
  
  bindex = lphash_bindex(lhtbl, p);

  return bindex2bin(lhtbl, bindex);
}

/* check if the table needs to be expanded; true if either it didn't need to be 
 * expanded, or else it expanded successfully. false if it failed.
 *
 */
bool lphash_expand_check(lphash_t* lhtbl){
  if((lhtbl->bincount < lhtbl->cfg.bincount_max) && (lphash_load(lhtbl) > lhtbl->cfg.max_load)){
    return lphash_expand_table(lhtbl);
  }
  return true;
}


static bool lphash_expand_directory(lphash_t* lhtbl, memcxt_t* memcxt){
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


static bool lphash_expand_table(lphash_t* lhtbl){
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

  memcxt = &(lhtbl->cfg.memcxt);

  success = add_size(lhtbl->maxp, lhtbl->p, &new_bindex);
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




bool lphash_insert(lphash_t* lhtbl, const void *key, const void *value){
  bucket_t* newbucket;
  bucket_t** binp;
  memcxt_t *memcxt;

  memcxt = &(lhtbl->cfg.memcxt);
  
  binp = lphash_fetch_bucket(lhtbl, key);

  newbucket = memcxt->allocate(BUCKET, sizeof(bucket_t));
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

void *lphash_lookup(lphash_t* lhtbl, const void *key){
  void* value;
  bucket_t** binp;
  bucket_t* bucketp;

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

bool lphash_delete(lphash_t* lhtbl, const void *key){
  bool found = false;
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;
  memcxt_t *memcxt;

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
      memcxt->release(BUCKET, current_bucketp, sizeof(bucket_t));

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
    lphash_contract_check(lhtbl);
  }
#endif
  
  return found;
}

size_t lphash_delete_all(lphash_t* lhtbl, const void *key){
  size_t count;
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;
  bucket_t* temp_bucketp;
  memcxt_t *memcxt;

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
      memcxt->release(BUCKET, temp_bucketp, sizeof(bucket_t));
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
    lphash_contract_check(lhtbl);
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

static void lphash_contract_check(lphash_t* lhtbl){
    /* iam: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (lphash_load(lhtbl) < lhtbl->cfg.min_load)){
      lphash_contract_table(lhtbl);
    }
}

/* assumes the non-null segments for an prefix of the directory */
static void lphash_contract_directory(lphash_t* lhtbl, memcxt_t* memcxt){
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
  if(! success ){
    return;
  }
  
  success = mul_size(oldlen, sizeof(segment_t*), &oldsz);

  assert(success);
  if(! success ){
    return;
  }

  assert(curlen < newlen);
  if(curlen >= newlen){
    return;
  }
  
  olddir = lhtbl->directory;
  
  newdir = memcxt->allocate(DIRECTORY, newsz);
  
  for(index = 0; index < newlen; index++){
    newdir[index] = olddir[index];
  }

  lhtbl->directory = newdir;
  lhtbl->directory_length = newlen;
  
  memcxt->release(DIRECTORY, olddir, oldsz);
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

static void lphash_contract_table(lphash_t* lhtbl){
  size_t seglen;
  size_t segindex;
  memcxt_t *memcxt;
  size_t srcindex;
  bucket_t** srcbin;
  size_t tgtindex;
  bucket_t** tgtbin;
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
    success = add_size(lhtbl->maxp, lhtbl->p - 1, &srcindex);
    assert(success);
    if( ! success ){
      return;
    }
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




#define BP_SCALE  32
/* one thing for every bit in the bitmask */
#define BP_LENGTH  BP_SCALE * 64  

#define SP_SCALE  8
/* one thing for every bit in the bitmask */
#define SP_LENGTH SP_SCALE * 64  

/* bucket_pool_t is defined in types.h */
struct bucket_pool_s {
  bucket_t pool[BP_LENGTH];       /* the pool of buckets; one for each bit in the bitmask array */
  uint64_t bitmasks[BP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use */
  size_t free_count;              /* the current count of free buckets in this pool             */
  void* next_bucket_pool;         /* the next bucket pool to look if this one is full           */
};

/* segment_pool_t is defined in types.h */
struct segment_pool_s {
  segment_t pool[SP_LENGTH];      /* the pool of segments; one for each bit in the bitmask array */
  uint64_t bitmasks[SP_SCALE];    /* the array of bitmasks; zero means: free; one means: in use  */
  size_t free_count;              /* the current count of free segments in this pool             */
  void* next_segment_pool;        /* the next segment pool to look if this one is full           */
};


//DD: is there a one-one or a many-to-one relationship b/w linhash tables and pools?
typedef struct pool_s {
  void *directory;              /* not sure if this is needed/desired */
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} pool_t;

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


static void* new_directory(pool_t* pool, size_t size){
  pool->directory = pool_mmap(pool->directory, size);
  return pool->directory;
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
#if 0
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
    for(bit = 0; bit < 64; bit++){
      if((mask & (((uint64_t)1) << bit)) == 0){
	bit_free_count++;
      }
    }
  }

  if(free_count != bit_free_count){
    fprintf(stderr, "sane_bucket_pool: free_count = %zu bit_free_count = %zu\n", free_count, bit_free_count);
    for(scale = 0; scale < BP_SCALE; scale++){
      fprintf(stderr, "\tbpool_current->bitmasks[%zu] = %"PRIu64"\n", scale, bpool->bitmasks[scale]);
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
#else
static inline bool sane_bucket_pool(bucket_pool_t *bpool) {
  return true;
}
#endif
#endif


static void init_pool(pool_t* pool){
  pool->directory = new_directory(pool, DIRECTORY_LENGTH * sizeof(void*));
  pool->segments = new_segments();
  pool->buckets = new_buckets();
}


// courtesy of BD 
#ifdef __GNUC__

static inline uint32_t ctz64(uint64_t x) {
  assert(x != 0);
  return __builtin_ctzl(x);
}

#else

static inline uint32_t ctz64(uint64_t x) {
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
static uint32_t get_free_bit(uint64_t mask){
  uint32_t index;
  uint64_t flipped;
  assert(mask != UINT64_MAX);
  flipped = ~mask;
  index = ctz64(flipped);  
  return index;
}

#ifndef NDEBUG
static inline bool get_bit(uint64_t mask, uint32_t index) {
  assert((0 <= index) && (index < 64));
  return (mask & (((uint64_t)1) << index)) ? true : false;
}
#endif

/* sets the bit specified by the index in the mask */
static inline uint64_t set_bit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < 64);
  return mask | (((uint64_t)1) << index);
}

/* clear the bit */
static inline uint64_t clear_bit(uint64_t mask, uint32_t index) {
  assert(0 <= index && index < 64);
  return mask & ~(((uint64_t)1) << index); 
}

static bucket_t* alloc_bucket(pool_t* pool){
  bucket_t *buckp;
  bucket_pool_t* bpool_current;
  size_t scale;
  size_t index;
  
  buckp = NULL;

  // BD: use a for loop
  for (bpool_current = pool->buckets; bpool_current != NULL; bpool_current = bpool_current->next_bucket_pool) {
    assert(sane_bucket_pool(bpool_current));

    if (bpool_current->free_count > 0) {

      /* lets go through the blocks looking for a free bucket */
      for(scale = 0; scale < BP_SCALE; scale++) {
	if(bpool_current->bitmasks[scale] < UINT64_MAX){

	  /* ok there should be one here; lets find it */
	  index = get_free_bit(bpool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));
	  buckp = &bpool_current->pool[(scale * 64) + index];
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
    bpool_current->next_bucket_pool = pool->buckets;
    pool->buckets = bpool_current;
    
    /* return the first bucket of the new pool */
    buckp = &bpool_current->pool[0];
    bpool_current->free_count --;
    bpool_current->bitmasks[0] = 1; // low-order bit is set
  
    assert(sane_bucket_pool(bpool_current));
    assert(buckp != NULL);
  }

  return buckp;
}

static bool free_bucket(pool_t* pool, bucket_t* buckp){
  bucket_pool_t* bpool;
  size_t index;
  size_t pmask_index;
  uint32_t pmask_bit;
  
  assert(pool != NULL);
  assert(buckp != NULL);

  /* get the bucket pool that we belong to */
  bpool = buckp->bucket_pool_ptr;

  /* sanity check */
  assert((bpool->pool <= buckp) && (buckp < bpool->pool + BP_LENGTH));
  assert(sane_bucket_pool(bpool));

  index = buckp - bpool->pool;

  pmask_index = index / 64;
  pmask_bit = index % 64;

  assert(get_bit(bpool->bitmasks[pmask_index], pmask_bit)); 

  bpool->bitmasks[pmask_index] = clear_bit(bpool->bitmasks[pmask_index], pmask_bit);
  bpool->free_count ++;
  
  assert((bpool->free_count > 0) && (bpool->free_count <= BP_LENGTH));
  assert(sane_bucket_pool(bpool));
  
  return true;
}


static segment_t* alloc_segment(pool_t* pool){
  segment_t *segp;
  segment_pool_t* spool_current;
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
	  index = get_free_bit(spool_current->bitmasks[scale]);

	  assert((0 <= index) && (index < 64));

	  /* ok we can use this one */
	  segp = &spool_current->pool[(scale * 64) + index];
	  spool_current->bitmasks[scale] = set_bit(spool_current->bitmasks[scale], index);
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
    
    spool_current = new_segments();
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

static bool free_segment(pool_t* pool, segment_t* segp){
  segment_pool_t* spool;

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

  assert(get_bit(spool->bitmasks[pmask_index], pmask_bit));
  spool->bitmasks[pmask_index] = clear_bit(spool->bitmasks[pmask_index], pmask_bit); 
  spool->free_count ++;

  assert((spool->free_count > 0) && (spool->free_count <= SP_LENGTH));
  
  return true;
}

/* just one for now */
static bool the_pool_is_ok = false;
static pool_t the_pool;

static void check_pool(void){
  if(!the_pool_is_ok){
    init_pool(&the_pool);
    the_pool_is_ok = true;
  }
}

static void *pool_allocate(pool_t* pool, memtype_t type, size_t size);

static void pool_release(pool_t* pool, memtype_t type, void *ptr, size_t size);



static void *_pool_allocate(memtype_t type, size_t size);

static void _pool_release(memtype_t type, void *ptr, size_t size);


void init_pool_memcxt(memcxt_t* pmem){
  if(pmem != NULL){
    pmem->allocate =  _pool_allocate;
    pmem->release = _pool_release;
  }
}

static void *_pool_allocate(memtype_t type, size_t size){
  return pool_allocate(&the_pool, type, size);
}

static void _pool_release(memtype_t type, void *ptr, size_t size){
  pool_release(&the_pool, type, ptr, size);
}



size_t pcount = 0;

static void *pool_allocate(pool_t* pool, memtype_t type, size_t size){
  void *memory;
  check_pool();
  switch(type){
  case DIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    // how many hash tables use this pool?
    memory = new_directory(pool, size);
    break;
  }
  case SEGMENT: {
    memory = alloc_segment(pool);
    break;
  }
  case BUCKET: {
    memory = alloc_bucket(pool);
    break;
  }
  default: assert(false);
    memory = NULL;
  }
  return memory;
}


static void pool_release(pool_t* pool, memtype_t type, void *ptr, size_t size){
  check_pool();
  switch(type){
  case DIRECTORY: {
    // not sure if maintaining the directory pointer makes much sense.
    // which enforces what: pool/linhash?
    pool_munmap(ptr, size);

    break;
  }
  case SEGMENT: {
    free_segment(pool, ptr);
    break;
  }
  case BUCKET: {
    free_bucket(pool, ptr);
    break;
  }
  default: assert(false);
  }
  
}


void dump_pool(FILE* fp){
  float bp = sizeof(bucket_pool_t);
  float sp = sizeof(segment_pool_t);
  bp /= 4096;
  sp /= 4096;
  fprintf(fp, "sizeof(bucket_pool_t) =  %zu\n", sizeof(bucket_pool_t));
  fprintf(fp, "pages: %f\n", bp);
  fprintf(fp, "sizeof(segment_pool_t) =  %zu\n", sizeof(segment_pool_t));
  fprintf(fp, "pages: %f\n", sp);

}

