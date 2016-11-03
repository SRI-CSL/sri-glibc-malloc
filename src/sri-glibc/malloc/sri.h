/*
 * Copyright (C) 2016  SRI International
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

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


/*
  SRI_HISTOGRAM in {0, 1}, DEFAULT is 0: This a diagnostic tool when
  dumping infomation about each arena's hash table. With the flag on we
  get a histogram of how long each chain is.
*/

#ifndef SRI_HISTOGRAM
#define SRI_HISTOGRAM  0
#endif


/* SRI_DUMP_LOOKUP in {0, 1}, DEFAULT is 0: This is a diagnostic tool, that
   will dump the state of the lookup hashtable as part of an assert failure.
   Very useful when metadata goes missing.
*/

#ifndef SRI_DUMP_LOOKUP
#define SRI_DUMP_LOOKUP  0
#endif



/* SRI_JENKINS_HASH in {0, 1}, DEFAULT is 0: This determines the 
hashing function used in each arena's hash table. If 1 then we use the
Jenkin's hash. If 0 we use the google hash. Both are defined in 
metadata.c.
*/

#ifndef SRI_JENKINS_HASH
#define SRI_JENKINS_HASH  0
#endif

/* SRI_POOL_DEBUG in {0, 1}, DEFAULT is 0: This truns on some serious
sanity checking of the memory pool. It will cause a dramitic slow down,
sometimes mistaken for haning by the impatient.
*/

#ifndef SRI_POOL_DEBUG
#define SRI_POOL_DEBUG 0
#endif



#endif
