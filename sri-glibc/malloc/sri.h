#ifndef _SRI_H
#define _SRI_H

/* We collect definitions that are specific to SRI's additions here.  */

/* 
   SRI_DEBUG_HEADERS in {0, 1}, Default is 0 :this is a useful flag to
   turn on when debugging internal development issues with the
   metadata. In particular when the metadata of a pointer is not
   found, there are two reasonable explanations. The pointer is not
   under our jurisdiction, or else it had metadata, but it was deleted
   (there are other possibilities but this is enough to motivate the
   flag.  When the flag is on (i.e. 1) then chunks have two
   INTERNAL_SIZE metadata slots:

struct malloc_chunk {
#if SRI_DEBUG_HEADERS
  INTERNAL_SIZE_T      __canary__;   // where prev_size use to live.
  INTERNAL_SIZE_T      arena_index;  // index of arena:  0: mmapped 1: Main Arena  N+1: Nth arena
#endif
};

  looking at the header of the "metadataless" chunk should clearly
  indicate which case is relevant. In the situation where the pointer
  is under our jurisdiction, the canary will contain a lot more
  information, such as whether and where it's metadata has been freed,
  and if it is active where it was originally registered.

*/


#ifndef SRI_DEBUG_HEADERS
#define SRI_DEBUG_HEADERS 0
#endif

/* 
   SRI_METADATA_CACHE in {0, 1}, DEFAULT is 0: This deal with caching
   in the dynamic hashtable (see metadata.[c,h]). If truned on the we
   cache the last *two* queries against the metadata
   hashtable. Simulations indicated that we were hitting diminishing
   returns with a fully associative cache of size 16 (roughly 50% hit
   rate on malloc-heavy benchmarks), but the cost of searching the cache
   when we missed was unacceptably high. Simulations showed a non-trivial
   benefit for very, very small caches, so we'll try that.
*/

#ifndef SRI_METADATA_CACHE
#define SRI_METADATA_CACHE  0
#endif




#endif



