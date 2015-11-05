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


#include "memcxt.h"

/*

  Notes:

  2^44 bytes on a godzilla machine
  
  minimum malloc chunk  16 <=  x  <=  64 

  Thus 2^38 <  # of chunks  < 2^40


 */

typedef struct bucket_s {
  void *key;
  void *value;
  void *next_bucket;
} bucket_t;

typedef bucket_t*  bucketptr;

typedef bucketptr*  segmentptr;

typedef struct linhash_cfg_s {

  bool multithreaded;               /* are we going to protect against contention                             */

  size_t segment_size;              /* segment size; larsen uses 256; we could use  4096 or 2^18 = 262144     */

  size_t initial_directory_size;    /* Larsen's directory is static ( also 256), ours will have to be dynamic */

  int16_t min_load;                 /* Not sure if Larsen ever specifies his value for this                   */

  int16_t max_load;                 /* Larsen uses 5 we could use  4 or 8                                     */

  memcxt_t memcxt;                  /* Where we get our memeory from                                          */

} linhash_cfg_t;


typedef struct linhash_s {

  linhash_cfg_t cfg;             /* configuration constants                                                */

  pthread_mutex_t mutex;	 /* lock for resolving contention    (only when cfg->multithreaded)        */

  segmentptr* directory;         /* the array of segment pointers                                          */

  size_t directory_size;         /* the size of the directory (must be a power of two)                     */

  size_t directory_current;      /* the number of segments in the directory                                */

  size_t N;                      /* mininum number of buckets    [{ N }]                                   */
  
  size_t L;                      /* the number of times the table has doubled in size  [{ L }]             */

  size_t p;                      /* index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }]  */

  size_t count;                  /* the total number of records in the table                               */

  size_t maxp;                   /* the current limit on the bucket count  [{ N * 2^L }]                   */

  size_t currentsize;            /* the current number of buckets                                          */
  
} linhash_t;





extern void init_linhash(linhash_t* lhash, memcxt_t* memcxt);

extern void linhash_insert(linhash_t* htbl, const void *key, const void *value);

extern void *linhash_lookup(linhash_t* htbl, const void *key);

extern void delete_linhash(linhash_t* htbl);




#endif


