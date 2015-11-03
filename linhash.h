#ifndef _LINHASH_H
#define _LINHASH_H
/*
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * [{ X }] refers to the concept X in Larson's paper.
 *
 */

#include <stdint.h>
#include <stdbool.h>


#define SEGMENT_SIZE               256

#define MINIMUM_TABLE_SIZE         256       /* [{ N }]  */

#define INITAL_DIRECTORY_SIZE      256       /* Larsen's directory is static, ours will have to be dynamic */

#define MIN_LOAD                   2

#define MAX_LOAD                   5

typedef void*  segmentptr;

typedef struct linhash_s {

  segmentptr directory;          /* the array of segment pointers                                          */

  size_t directory_size;         /* the size of the directory (must be a power of two)                     */

  size_t directory_current;      /* the number of segments in the directory                                */
  
  size_t growth_factor;          /* the number of times the table has doubled in size  [{ L }]             */

  size_t next_bucket;            /* index of the next bucket due to be split  [{ p :  0 <= p < N * 2^L }]  */

  size_t count;                  /* the total number of records in the table                               */

  size_t bucket_bound            /* the current limit on the bucket count N * 2^L                          */
  
} linhash_t;








#endif


