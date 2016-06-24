#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "metadata.h"
#include "utils.h"


const bool      metadata_multithreaded             = false;

const size_t    metadata_segment_length            = SEGMENT_LENGTH;
const size_t    metadata_initial_directory_length  = DIRECTORY_LENGTH;
const size_t    metadata_segments_at_startup       = 1;

const uint16_t  metadata_min_load                 = 2;   
const uint16_t  metadata_max_load                 = 3;

/* toggle for enabling table contraction */
#define CONTRACTION_ENABLED  1

/* static routines */
static void metadata_cfg_init(metadata_cfg_t* cfg, memcxt_t* memcxt);

/* metadata expansion routines */
static bool metadata_expand_check(metadata_t* lhtbl);

static bool metadata_expand_directory(metadata_t* lhtbl, memcxt_t* memcxt);

/* split's the current bin  */
static bool metadata_expand_table(metadata_t* lhtbl);
 
#if CONTRACTION_ENABLED
/* linhash contraction routines */

static void metadata_contract_check(metadata_t* lhtbl);

static void metadata_contract_directory(metadata_t* lhtbl, memcxt_t* memcxt);

static void metadata_contract_table(metadata_t* lhtbl);

#endif

/* returns the bin index (bindex) of the bin that should contain p  [{ hash }] */
static uint32_t metadata_bindex(metadata_t* lhtbl, const void *p);

/* returns a pointer to the bin i.e. the top bucket at the given bindex  */
static bucket_t** bindex2bin(metadata_t* lhtbl, uint32_t bindex);

/* returns the length of the linked list starting at the given bucket */
static size_t bucket_length(bucket_t* bucket);

#ifdef SRI_HISTOGRAM 
/* drastic data dump  */
static void bucket_dump(int fd, bucket_t* bucket);
#endif

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

static inline size_t metadata_load(metadata_t* lhtbl){
  return lhtbl->count / lhtbl->bincount;
}

static void metadata_cfg_init(metadata_cfg_t* cfg, memcxt_t* memcxt){
  cfg->multithreaded            = metadata_multithreaded;
  cfg->segment_length           = metadata_segment_length;
  cfg->initial_directory_length = metadata_initial_directory_length;
  cfg->min_load                 = metadata_min_load;   
  cfg->max_load                 = metadata_max_load;
  cfg->memcxt                   = memcxt;
  cfg->bincount_max             = UINT32_MAX;
  cfg->directory_length_max     = cfg->bincount_max / SEGMENT_LENGTH;

  //iam: should these be more than just asserts?
  assert(is_power_of_two(cfg->segment_length));
  assert(is_power_of_two(cfg->initial_directory_length));

}



bool init_metadata(metadata_t* lhtbl, memcxt_t* memcxt){
  metadata_cfg_t* lhtbl_cfg;
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
  
  metadata_cfg_init(lhtbl_cfg, memcxt);
    

  lhtbl->directory_length = lhtbl_cfg->initial_directory_length;
  lhtbl->directory_current = metadata_segments_at_startup; 

  
  /* the size of the directory */
  success = mul_size(lhtbl->directory_length, sizeof(segment_t*), &dirsz);
  if(!success){
    errno = EINVAL;
    return false;
  }
    
  /* the directory; i.e. the array of segment pointers */
  lhtbl->directory = memcxt_allocate(memcxt, DIRECTORY, NULL, dirsz);
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
    seg = (segment_t*)memcxt_allocate(memcxt, SEGMENT, NULL, sizeof(segment_t));
    lhtbl->directory[index] = seg;
    if(seg == NULL){
      errno = ENOMEM;
      return false;
    }
  }

  /* No WTF's to start. */
  lhtbl->wtf1 = lhtbl->wtf2  = 0;

  return true;
}

#ifdef SRI_HISTOGRAM 

const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31};

int log2_32 (uint32_t value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}

#endif

extern void dump_metadata(FILE* fp, metadata_t* lhtbl, bool showloads){
  size_t index;
  size_t blen;
  bucket_t** bp;

  size_t maxlength;
  size_t maxindex;

#ifdef SRI_HISTOGRAM 
  static uint32_t dumpcount = 0;
  char dumpfile[1024] = {'\0'};
  uint32_t histogram[33] = {0};
  uint32_t hist_limit = 0;
#endif
  
  
  fprintf(fp, "directory_length = %" PRIuPTR "\n", lhtbl->directory_length);
  fprintf(fp, "directory_current = %" PRIuPTR "\n", lhtbl->directory_current);
  fprintf(fp, "N = %" PRIuPTR "\n", lhtbl->N);
  fprintf(fp, "L = %" PRIuPTR "\n", lhtbl->L);
  fprintf(fp, "count = %" PRIuPTR "\n", lhtbl->count);
  fprintf(fp, "maxp = %" PRIuPTR "\n", lhtbl->maxp);
  fprintf(fp, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
  fprintf(fp, "load = %" PRIuPTR "\n", metadata_load(lhtbl));
  fprintf(fp, "wtf1 = %"PRIu64", wtf2 = %"PRIu64"\n", lhtbl->wtf1, lhtbl->wtf2);

  maxlength = 0;
  maxindex = 0;
  
  if(showloads){
    fprintf(fp, "bucket lengths: ");
  }

  for(index = 0; index < lhtbl->bincount; index++){
    bp = bindex2bin(lhtbl, index);
    blen = bucket_length(*bp);

#ifdef SRI_HISTOGRAM 
    if( blen == 0){
      histogram[0]++;
    } else {
      histogram[log2_32((uint32_t)blen) + 1]++;
    }
#endif

    if( blen > maxlength ){ 
      maxlength = blen; 
      maxindex = index;
    }

    if(showloads && blen != 0){
      fprintf(fp, "%" PRIuPTR ":%" PRIuPTR " ", index, blen);
    }

  }




  fprintf(fp, "\n");
  fprintf(fp, "maximum length = %" PRIuPTR " @ index %zu\n", maxlength, maxindex);

#ifdef SRI_HISTOGRAM 

#ifndef SRI_NOT_JENKINS
  const char hashname[] = "jenkins";
#else
  const char hashname[] = "google";
#endif

  snprintf(dumpfile, 1024, "/tmp/bin_%d_%zu_%s.txt", dumpcount++, maxindex, hashname);
  
  int fd = open(dumpfile, O_WRONLY | O_CREAT, 00777);
  
  
  if (fd != -1){
    bp = bindex2bin(lhtbl, maxindex);
    bucket_dump(fd, *bp);
    close(fd);
  }
  hist_limit = (log2_32(maxlength) + 2 < 33) ? (log2_32(maxlength) + 2) : 33;

  for(index = 0; index < hist_limit; index++){
    fprintf(fp, "histogram[%zu] =\t%"PRIu32"\n", index, histogram[index]);
  }
#endif
}


void delete_metadata(metadata_t* lhtbl){
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
	  memcxt_release(memcxt, BUCKET, current_bucket, sizeof(bucket_t));
	  current_bucket = next_bucket;
	}
      }
      /* now release the segment */
      memcxt_release(memcxt, SEGMENT, current_segment,  sizeof(segment_t));
  }
  
  success = mul_size(lhtbl->directory_length, sizeof(segment_t*), &dirsz);
  assert(success);
  if(success){
    memcxt_release(memcxt, DIRECTORY, lhtbl->directory, dirsz);
  }
}

/* https://github.com/google/farmhash/blob/master/src/farmhash.h */

// This is intended to be a good fingerprinting primitive.
inline uint64_t Fingerprint(uint64_t x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t b = x * kMul;
  b ^= (b >> 44);
  b *= kMul;
  b ^= (b >> 41);
  b *= kMul;
  return b;
}


/* returns the raw bindex/index of the bin that should contain p  [{ hash }] */
static uint32_t metadata_bindex(metadata_t* lhtbl, const void *p){
  uint32_t jhash;
  uint32_t l;
  size_t next_maxp;

#ifndef SRI_NOT_JENKINS
  jhash  = jenkins_hash_ptr(p);
#else
  jhash  = (uint32_t)Fingerprint((uintptr_t)p);
#endif

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

bucket_t** bindex2bin(metadata_t* lhtbl, uint32_t bindex){
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
bucket_t** metadata_fetch_bucket(metadata_t* lhtbl, const void *p){
  uint32_t bindex;
  
  bindex = metadata_bindex(lhtbl, p);

  return bindex2bin(lhtbl, bindex);
}

/* check if the table needs to be expanded; true if either it didn't need to be 
 * expanded, or else it expanded successfully. false if it failed.
 *
 */
bool metadata_expand_check(metadata_t* lhtbl){
  if((lhtbl->bincount < lhtbl->cfg.bincount_max) && (metadata_load(lhtbl) > lhtbl->cfg.max_load)){
    return metadata_expand_table(lhtbl);
  }
  return true;
}


static bool metadata_expand_directory(metadata_t* lhtbl, memcxt_t* memcxt){
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
  newdir = memcxt_allocate(memcxt, DIRECTORY, olddir, new_dirsz);

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
  memcxt_release(memcxt, DIRECTORY, olddir, old_dirsz);

  return true;
}


static bool metadata_expand_table(metadata_t* lhtbl){
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
    if(! metadata_expand_directory(lhtbl,  memcxt)){
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
      newseg = memcxt_allocate(memcxt, SEGMENT, NULL, sizeof(segment_t));
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

      if(metadata_bindex(lhtbl, current->chunk) == new_bindex){

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

bool metadata_add(metadata_t* lhtbl, bucket_t* newbucket){
  bucket_t** binp;

  binp = metadata_fetch_bucket(lhtbl, newbucket->chunk);

  /* for the time being we insert the bucket at the front */
  newbucket->next_bucket = *binp;
  *binp = newbucket;

  /* census adjustments */
  lhtbl->count++;

  /* check to see if we need to exand the table */
  return metadata_expand_check(lhtbl);
}

#ifdef SRI_WTF
static inline bucket_t *internal_md_lookup(bucket_t **binp, const void *chunk)
{
  bucket_t* value;
  bucket_t* bucketp;

  value = NULL;
  bucketp = *binp;

  while(bucketp != NULL){
    if(chunk == bucketp->chunk){
      value = bucketp;
      break;
    }
    bucketp = bucketp->next_bucket;
  }

  return value;
}

static inline bool internal_md_delete(metadata_t *lhtbl, bucket_t **binp, const void *chunk)
{
  bool found = false;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;

  previous_bucketp = NULL;
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    if(chunk == current_bucketp->chunk){
      found = true;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      memcxt_release(lhtbl->cfg.memcxt, BUCKET, current_bucketp, sizeof(bucket_t));

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
    metadata_contract_check(lhtbl);
  }
#endif

  return found;
}

bool metadata_add(metadata_t* lhtbl, bucket_t* newbucket){
  bucket_t** binp;
  // bucket_t *lookup_result; // Changing to unconditional delete...

  binp = metadata_fetch_bucket(lhtbl, newbucket->chunk);
#if 0
  // Drew is commenting out for unconditional delete...
  lookup_result = internal_md_lookup(binp, newbucket->chunk);

  if(lookup_result == newbucket) {
      /* This is a type-1 WTF, count & return */
      lhtbl->wtf1++;
  } else if (lookup_result != NULL) {
    lhtbl->wtf2++;
    (void)internal_md_delete(lhtbl, binp, newbucket->chunk);
    // if(lhtbl->wtf2 > 1000) {abort(); }
  }
#else
  if(metadata_delete(lhtbl, newbucket->chunk)) {
    lhtbl->wtf2++;
  }
#endif

  /* for the time being we insert the bucket at the front */
  newbucket->next_bucket = *binp;
  *binp = newbucket;
    
  /* census adjustments */
  lhtbl->count++;
  
    /* check to see if we need to exand the table */
  return metadata_expand_check(lhtbl);
}

bool metadata_delete(metadata_t* lhtbl, const void *chunk){

  bucket_t** binp;

  binp = metadata_fetch_bucket(lhtbl, chunk);
  return internal_md_delete(lhtbl, binp, chunk);

 }

bucket_t* metadata_lookup(metadata_t* lhtbl, const void *chunk){

  bucket_t** binp;

  binp = metadata_fetch_bucket(lhtbl, chunk);
  return internal_md_lookup(binp, chunk);
}

#else 


bucket_t* metadata_lookup(metadata_t* lhtbl, const void *chunk){
  bucket_t* value;
  bucket_t** binp;
  bucket_t* bucketp;

  value = NULL;
  binp = metadata_fetch_bucket(lhtbl, chunk);
  bucketp = *binp;

  while(bucketp != NULL){
    if(chunk == bucketp->chunk){
      value = bucketp;
      break;
    }
    bucketp = bucketp->next_bucket;
  }

  return value;
}

bool metadata_delete(metadata_t* lhtbl, const void *chunk){
  bool found = false;
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;

  previous_bucketp = NULL;
  binp = metadata_fetch_bucket(lhtbl, chunk);
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    if(chunk == current_bucketp->chunk){
      found = true;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      memcxt_release(lhtbl->cfg.memcxt, BUCKET, current_bucketp, sizeof(bucket_t));

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
    metadata_contract_check(lhtbl);
  }
#endif

  return found;
}

#endif

size_t metadata_delete_all(metadata_t* lhtbl, const void *chunk){
  size_t count;
  bucket_t** binp;
  bucket_t* current_bucketp;
  bucket_t* previous_bucketp;
  bucket_t* temp_bucketp;

  count = 0;
  previous_bucketp = NULL;
  binp = metadata_fetch_bucket(lhtbl, chunk);
  current_bucketp = *binp;

  while(current_bucketp != NULL){
    
    if(chunk == current_bucketp->chunk){
      count++;
      
      if(previous_bucketp == NULL){
 	*binp = current_bucketp->next_bucket;
      } else {
	previous_bucketp->next_bucket = current_bucketp->next_bucket;
      }
      temp_bucketp = current_bucketp;
      current_bucketp = current_bucketp->next_bucket;
      memcxt_release(lhtbl->cfg.memcxt, BUCKET, temp_bucketp, sizeof(bucket_t));
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
    metadata_contract_check(lhtbl);
  }
#endif

  return count;
}


#ifdef SRI_HISTOGRAM 
void bucket_dump(int fd, bucket_t* bucket){
  bucket_t* current;

  current = bucket;
  while(current != NULL){
    char buff[64] = {'\0'};
    snprintf(buff, 64, "%p:%p\n", current->chunk, bucket);
    write(fd, buff, strlen(buff));
    current = current->next_bucket;
  }

}
#endif


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

static void metadata_contract_check(metadata_t* lhtbl){
    /* iam: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (metadata_load(lhtbl) < lhtbl->cfg.min_load)){
      metadata_contract_table(lhtbl);
    }
}

/* assumes the non-null segments for an prefix of the directory */
static void metadata_contract_directory(metadata_t* lhtbl, memcxt_t* memcxt){
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
  if( ! success ){
    return;
  }
  
  success = mul_size(oldlen, sizeof(segment_t*), &oldsz);

  assert(success);
  if( ! success ){
    return;
  }

  assert(curlen < newlen);
  if( ! (curlen < newlen) ){
    return;
  }
  
  olddir = lhtbl->directory;
  
  newdir = memcxt_allocate(memcxt, DIRECTORY, olddir, newsz);
  
  if (olddir != newdir){
    for(index = 0; index < newlen; index++){
      newdir[index] = olddir[index];
    }
    
    lhtbl->directory = newdir;
  }

  lhtbl->directory_length = newlen;
  
  memcxt_release(memcxt, DIRECTORY, olddir, oldsz);
}

static inline void check_index(size_t index, const char* name, metadata_t* lhtbl){
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

static void metadata_contract_table(metadata_t* lhtbl){
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
    metadata_contract_directory(lhtbl,  memcxt);
  }

  
  /* get the two buckets involved; moving src to tgt */
  if(lhtbl->p == 0){
    tgtindex = (lhtbl->maxp >> 1) - 1;
    srcindex = lhtbl->maxp - 1;
  } else {
    tgtindex = lhtbl->p - 1;
    success = add_size(lhtbl->maxp, lhtbl->p - 1, &srcindex);
    assert(success);
    if(! success ){
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
    memcxt_release(memcxt, SEGMENT, lhtbl->directory[segindex], sizeof(segment_t));
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

