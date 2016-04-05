#include <malloc.h>
#include "ckallocator.h"

static size_t allocated = 0;


size_t ck_allocated(void){
  return allocated;
}


/*

https://groups.google.com/forum/#!topic/concurrencykit/GezcuxYLPs0

The extra arguments to the free callback are number of bytes of region
being deallocated and bool indicating whether the memory being
destroyed is vulnerable to read-reclaim races (and so, extra
precautions must be taken).

For realloc, the first size_t is current number of bytes of allocation
and the second is the new number of bytes. The bool indicates the same
thing as free, whether safe memory reclamation of some form might be
needed.

*/

static void *
ht_malloc(size_t r)
{
  allocated += r;
  return lfpa_malloc(r);
}

static void
ht_free(void *p, size_t b, bool r)
{
  (void)b;
  (void)r;
  allocated -= b;
  lfpa_free(p);
  return;
}

static void *
ht_realloc(void *p, size_t os, size_t ns, bool r)
{
  (void)os;
  (void)r;
  return lfpa_realloc(p, ns);
}


bool ck_allocator_init(struct ck_malloc* allocator){
  if(allocator != NULL){
    allocator->malloc = ht_malloc;
    allocator->realloc = ht_realloc;
    allocator->free = ht_free;
    return true;
  }
  return false;
}

