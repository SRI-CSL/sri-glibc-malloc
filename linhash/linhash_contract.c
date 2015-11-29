static void linhash_contract_table(linhash_t* lhtbl);

static void linhash_contract_check(linhash_t* lhtbl){
    /* iam Q4: better make sure that immediately after an expansion we don't drop below the min_load!! */
  if((lhtbl->L > 0) && (linhash_load(lhtbl) < lhtbl->cfg.min_load)){
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

static inline void check_index(size_t index, const char* name, linhash_t* lhtbl){
  if( index >= lhtbl->bincount ){
    fprintf(stderr, "%s index = %" PRIuPTR "\n", name, index);
    fprintf(stderr, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
    fprintf(stderr, "lhtbl->maxp = %" PRIuPTR "\n", lhtbl->maxp);
    fprintf(stderr, "lhtbl->p = %" PRIuPTR "\n", lhtbl->p);
  }
  assert( index < lhtbl->bincount);
}

/* move all the buckets in the src bin to the tgt bin */
static void move_buckets(bucket_t** srcbin, bucket_t** tgtbin){
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
     iam Q5: need to ensure we don't get unwanted oscillations;
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


  /* 
   * here be the bug (???): if lhtbl->p = 0 then we cannot just move one
   * bin, we have to move half of them. so we should make sure that
   * moving half keeps the load low.
   *
   * if there is such a bug, why can't I tickle it?
   */

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
