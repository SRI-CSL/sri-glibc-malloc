#ifndef _METADATA_H
#define _METADATA_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <stdio.h>

///#include "types.h"
#include "memcxt.h"



/*
 *
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * [{ X }] refers to the concept X in Larson's paper.
 *
 *
 * The directory is an expandable array of segments.  Each segment is
 * essentially a fixed size array of bins.  Each bin is essentially a
 * linked list of buckets.  Each bin is "addressed" by its hash.  The
 * hash is currently uint32_t. So the maximum number of bins is also
 * uint32_t.
 *
 * We either we run out of memory prior to achieving the maximum number
 * of bins, or else we achieve the maximum.
 *
 * Once the maximum number of bins has been reached, we should still
 * be able to function as a hash table, the load will just start to
 * increase, as the linked lists of buckets get longer.  The only real
 * *catastrophic* failure should be when we can no longer create
 * buckets. 
 *
 */


typedef struct metadata_cfg_s {
  memcxt_t *memcxt;                 /* Where we get our memory from                                            */
  size_t segment_length;            /* segment length; larsen uses 256; we could use  4096 or 2^18 = 262144    */
  size_t initial_directory_length;  /* Larsen's directory is static ( also 256), ours will have to be dynamic  */
  size_t directory_length_max;      /* Currently don't get this big (see note following this)                  */
  size_t bincount_max;              /* the maximum number of bins: directory_length_max * segment_length       */
  uint16_t min_load;                /* Not sure if Larsen ever specifies his value for this                    */
  uint16_t max_load;                /* Larsen uses 5 we could use  4 or 8                                      */
  bool multithreaded;               /* are we going to protect against contention                              */
} metadata_cfg_t;

/*

  Notes (ddean, iam):

  2^44 bytes on a godzilla machine
  
  minimum malloc chunk  16 <=  x  <=  64 

  Thus 2^38 <  # of chunks  < 2^40

  If we settle on a segment_length of between 4096 = 2^12  and 262144 = 2^18

  Then  [2^20,  2^26] <= directory_length_max <= [2^22,  2^28]

 */



typedef struct metadata_s {
  metadata_cfg_t cfg;             /* configuration constants                                                */
  pthread_mutex_t mutex;	 /* lock for resolving contention    (only when cfg->multithreaded)        */
  segment_t** directory;         /* the array of segment pointers                                          */
  size_t directory_length;       /* the size of the directory (must be a power of two)                     */
  size_t directory_current;      /* the number of segments in the directory                                */
  size_t N;                      /* mininum number of buckets    [{ N }]                                   */
  size_t L;                      /* the number of times the table has doubled in size  [{ L }]             */
  size_t p;                      /* index of the next bin due to be split  [{ p :  0 <= p < N * 2^L }]     */
  size_t count;                  /* the total number of records in the table                               */
  size_t maxp;                   /* the current limit on the bin count  [{ maxp = N * 2^L }]               */
  size_t bincount;               /* the current number of bins                                             */
} metadata_t;




/* 
 * Initializes a metadata_t object; returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 */
extern bool init_metadata(metadata_t* lhash, memcxt_t* memcxt);

extern void delete_metadata(metadata_t* htbl);

/* 
 * Adds the bucket to the table according to the chunk. Returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It is allowed to insert the same bucket multiple times. The buckets accumulate in this case.
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 */

extern bool metadata_add(metadata_t* htbl, chunkinfoptr bucket);

/*
 * Returns the (first) bucket associated with the chunk in the table, and NULL
 * if it is not found.
 *
 */
extern chunkinfoptr metadata_lookup(metadata_t* htbl, const void *chunk);

/* deletes the first bucket keyed by chunk; returns true if such a bucket was found; false otherwise */
extern bool metadata_delete(metadata_t* htbl, const void *chunk);

/* deletes all buckets keyed by chunk; returns the number of buckets deleted */
extern size_t metadata_delete_all(metadata_t* htbl, const void *chunk);

extern void dump_metadata(FILE* fp, metadata_t* lhash, bool showloads);

static inline chunkinfoptr allocate_chunkinfoptr(metadata_t* htbl){
  chunkinfoptr retval =  memcxt_allocate(htbl->cfg.memcxt, BUCKET, sizeof(bucket_t));
  retval->prev_size = 0; 
  retval->size = 0; 
  retval->req = 0; 
  retval->chunk = NULL; 
  retval->next_bucket = NULL; 
  return retval;
}

static inline void release_chunkinfoptr(metadata_t* htbl, chunkinfoptr bucket){
  memcxt_release(htbl->cfg.memcxt, BUCKET, bucket, sizeof(bucket_t));
}

static inline bool metadata_insert (metadata_t* htbl, chunkinfoptr ci_orig, chunkinfoptr ci_insert){
  return metadata_add(htbl, ci_insert);
}

static inline bool metadata_skiprm (metadata_t* htbl, chunkinfoptr ci_orig, chunkinfoptr ci_todelete){
  return metadata_delete(htbl, ci_todelete->chunk);
}

static inline bool metadata_insert_chunk(metadata_t* htbl, void * chunk){
  chunkinfoptr newb = allocate_chunkinfoptr(htbl);
  if(newb != NULL){
    newb->chunk = chunk;
    return metadata_add(htbl, newb);
  }
  return false;
}

#endif


