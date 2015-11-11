#ifndef _LINHASH_H
#define _LINHASH_H

/*
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * [{ X }] refers to the concept X in Larson's paper.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <stdio.h>

#include "types.h"
#include "memcxt.h"


#define SEGMENT_LENGTH 256

#define DIRECTORY_LENGTH 1024


/* need to make a distinction between bins and buckets  */
/* a bin is the address of an element of a segment       */ 
/* need a consistent terminology about bins and offsets */

/* the code for contracting a table seems to be missing in Larsen's paper */



typedef struct bucket_s {
  void *key;
  void *value;
  void *next_bucket;
  bucket_pool_t * bucket_pool_ptr;  //BD's optimization.
} bucket_t;

typedef bucket_t*  bucketptr;

typedef bucketptr*  segmentptr;

typedef struct linhash_cfg_s {

  memcxt_t memcxt;                  /* Where we get our memory from                                           */

  size_t segment_size;              /* segment size; larsen uses 256; we could use  4096 or 2^18 = 262144     */

  size_t initial_directory_size;    /* Larsen's directory is static ( also 256), ours will have to be dynamic */

  size_t directory_size_max;        /* Currently don't get this big (see note following this)                 */

  size_t address_max;               /* directory_size_max *  segment_size                                     */
  
  uint16_t min_load;                 /* Not sure if Larsen ever specifies his value for this                   */

  uint16_t max_load;                 /* Larsen uses 5 we could use  4 or 8                                     */

  bool multithreaded;               /* are we going to protect against contention                             */


} linhash_cfg_t;

/*

  Notes (ddean, iam):

  2^44 bytes on a godzilla machine
  
  minimum malloc chunk  16 <=  x  <=  64 

  Thus 2^38 <  # of chunks  < 2^40

  If we settle on a segment_size of between 4096 = 2^12  and 262144 = 2^18

  Then  [2^20,  2^26] <= directory_size_max <= [2^22,  2^28]

  
  Thus 

 */



typedef struct linhash_s {

  linhash_cfg_t cfg;             /* configuration constants                                                */

  pthread_mutex_t mutex;	 /* lock for resolving contention    (only when cfg->multithreaded)        */

  segmentptr* directory;         /* the array of segment pointers                                          */

  size_t directory_size;         /* the size of the directory (must be a power of two)                     */

  size_t directory_current;      /* the number of segments in the directory                                */

  size_t N;                      /* mininum number of buckets    [{ N }]                                   */
  
  size_t L;                      /* the number of times the table has doubled in size  [{ L }]             */

  size_t p;                      /* index of the next bin due to be split  [{ p :  0 <= p < N * 2^L }]     */

  size_t count;                  /* the total number of records in the table                               */
 
  size_t maxp;                   /* the current limit on the bin count  [{ maxp = N * 2^L }]               */

  size_t currentsize;            /* the current number of bins                                             */
  
} linhash_t;





extern void init_linhash(linhash_t* lhash, memcxt_t* memcxt);

extern void delete_linhash(linhash_t* htbl);

extern void linhash_insert(linhash_t* htbl, const void *key, const void *value);

extern void *linhash_lookup(linhash_t* htbl, const void *key);

/* deletes the first bucket keyed by key; returns true if such a bucket was found; false otherwise */
extern bool linhash_delete(linhash_t* htbl, const void *key);

/* deletes all buckets keyed by key; returns the number of buckets deleted */
extern size_t linhash_delete_all(linhash_t* htbl, const void *key);

extern void dump_linhash(FILE* fp, linhash_t* lhash, bool showloads);




#endif


