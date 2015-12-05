#ifndef _LPHASH_H
#define _LPHASH_H

/*
 *  Self contained version of linhash using pools.
 *  Main client: replay.
 * 
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <stdio.h>


typedef struct lphash_s lphash_t;

typedef struct lpbucket_pool_s lpbucket_pool_t;

typedef struct lpsegment_pool_s lpsegment_pool_t;

typedef struct lpmemcxt_s lpmemcxt_t;


#define LPSEGMENT_LENGTH 256

#define LPDIRECTORY_LENGTH 1024


/*
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


/* the code for contracting a table seems to be missing in Larsen's paper */

typedef struct lpbucket_s {
  void *key;
  void *value;
  void *next_bucket;
  lpbucket_pool_t * bucket_pool_ptr;  //BD's optimization #1.
} lpbucket_t;


typedef struct lpsegment_s {
  lpbucket_t* segment[LPSEGMENT_LENGTH];
  lpsegment_pool_t *segment_pool_ptr;  
} lpsegment_t;

typedef enum { LPDIRECTORY, LPSEGMENT, LPBUCKET } lpmemtype_t;


struct lpmemcxt_s {
  void *(*allocate)(lpmemtype_t, size_t);
  void (*release)(lpmemtype_t, void*, size_t);
};



typedef struct lphash_cfg_s {

  lpmemcxt_t memcxt;                /* Where we get our memory from                                            */
 
  size_t segment_length;            /* segment length; larsen uses 256; we could use  4096 or 2^18 = 262144    */

  size_t initial_directory_length;  /* Larsen's directory is static ( also 256), ours will have to be dynamic  */

  size_t directory_length_max;      /* Currently don't get this big (see note following this)                  */

  size_t bincount_max;              /* the maximum number of bins: directory_length_max * segment_length       */
  
  uint16_t min_load;                /* Not sure if Larsen ever specifies his value for this                    */

  uint16_t max_load;                /* Larsen uses 5 we could use  4 or 8                                      */

  bool multithreaded;               /* are we going to protect against contention                              */


} lphash_cfg_t;


struct lphash_s {

  lphash_cfg_t cfg;             /* configuration constants                                                */

  pthread_mutex_t mutex;	 /* lock for resolving contention    (only when cfg->multithreaded)        */

  lpsegment_t** directory;         /* the array of segment pointers                                          */

  size_t directory_length;       /* the size of the directory (must be a power of two)                     */

  size_t directory_current;      /* the number of segments in the directory                                */

  size_t N;                      /* mininum number of buckets    [{ N }]                                   */
  
  size_t L;                      /* the number of times the table has doubled in size  [{ L }]             */

  size_t p;                      /* index of the next bin due to be split  [{ p :  0 <= p < N * 2^L }]     */

  size_t count;                  /* the total number of records in the table                               */
 
  size_t maxp;                   /* the current limit on the bin count  [{ maxp = N * 2^L }]               */

  size_t bincount;               /* the current number of bins                                             */
  
};



/* 
 * Initializes a lphash_t object; returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 */
extern bool init_lphash(lphash_t* lhash);

extern void delete_lphash(lphash_t* htbl);

/* 
 * Inserts the key value pair into the has table. Returns true if successful; false if not.
 * If it returns false it sets errno to explain the error.
 * It is allowed to insert multiple entries for a particular key. The key-value pairs
 * accumulate in this case.
 *
 * It can fail due to:
 *   -- lack of memory   errno = ENOMEM.
 *   -- bad arguments    errno = EINVAL.
 */
extern bool lphash_insert(lphash_t* htbl, const void *key, const void *value);

/*
 * Returns the (first) value associated with the key in the table, and NULL
 * if it is not found.
 *
 */
extern void *lphash_lookup(lphash_t* htbl, const void *key);

/* deletes the first bucket keyed by key; returns true if such a bucket was found; false otherwise */
extern bool lphash_delete(lphash_t* htbl, const void *key);

/* deletes all buckets keyed by key; returns the number of buckets deleted */
extern size_t lphash_delete_all(lphash_t* htbl, const void *key);

extern void dump_lphash(FILE* fp, lphash_t* lhash, bool showloads);


#endif


