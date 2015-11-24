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
    fprintf(stderr, "lhtbl->maxp = %" PRIuPTR "\n", lhtbl->maxp);
    fprintf(stderr, "lhtbl->p = %" PRIuPTR "\n", lhtbl->p);
    fprintf(stderr, "srcindex = %" PRIuPTR "\n", srcindex);
    fprintf(stderr, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
  }
  assert( srcindex < lhtbl->bincount);
  
  srcbucketp = bindex2bin(lhtbl, srcindex);

  if(srcbucketp == NULL){
    fprintf(stderr, "srcindex = %" PRIuPTR "\n", srcindex);
    fprintf(stderr, "bincount = %" PRIuPTR "\n", lhtbl->bincount);
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

      fprintf(stderr, "TARGET BUCKET EMPTY tgtindex = %" PRIuPTR "\n", tgtindex);

      /* not very likely */
      *tgtbucketp = src;

    } else {
      /* easiest is to splice the src bin onto the end of the tgt */
      while(src->next_bucket != NULL){  src = src->next_bucket; }
      src->next_bucket = tgt;
    }
  } else {
    fprintf(stderr, "SOURCE BUCKET EMPTY srcindex = %" PRIuPTR "\n", srcindex);
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
