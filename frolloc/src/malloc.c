/*
 * Copyright (C) 2007  Scott Schneider, Christos Antonopoulos
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

#include <inttypes.h>
#include <pthread.h>

#include "malloc.h"
#include "malloc_internals.h"
#include "lfht.h"
#include "util.h"
#include "debug.h"

/* 
   Just a placeholder to mark where we *really* should be using
   lfht_delete if we had it. Note that in our world 0 is an 
   invalid value for either a descriptor ptr or the size of
   a mmapped region.
*/
#define TOMBSTONE 0

static sizeclass sizeclasses[MAX_BLOCK_SIZE / GRANULARITY];

/* Currently the same size as SBSIZE */
#define DESC_HTABLE_CAPACITY 16*4096

/* Currently pulled out of a hat with the rabbit  */
#define MMAP_HTABLE_CAPACITY 4096*4096

static lfht_t desc_tbl;  // maps superblock ptr --> desc
static lfht_t mmap_tbl;  // maps mmapped region --> size

/*
  The attribute __constructor__ mechanism cannot be safely relied upon
  to guarantee initialization prior to use. So we rely on a single
  lock. Once the library has been successfully initialized we never
  touch the lock again. But here it is, in all it's non-lock-free
  glory.
*/
static volatile bool __initialized__ = false;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static __thread procheap* heaps[MAX_BLOCK_SIZE / GRANULARITY] =  { };

/* 
   the alignment voodoo is because clang gets it wrong on darwin and the 
   cas_128  (i.e. cmpxchg16b) requires 16 byte alignment
*/
static volatile descriptor_queue queue_head __attribute__ ((aligned (16)));

static void init_sizeclasses(void)
{
  int i;
  const int length = MAX_BLOCK_SIZE / GRANULARITY;
  for(i = 0; i < length; i++){ 
    sizeclasses[i].sz = GRANULARITY * (i + 1);
    sizeclasses[i].sbsize = SBSIZE;
  }
}

void frolloc_init(void) 
{
  pthread_mutex_lock(&lock);
  if( __initialized__ ){
    pthread_mutex_unlock(&lock);
    return;
  }
  log_init();
  init_sizeclasses();
  if( ! init_lfht(&desc_tbl, DESC_HTABLE_CAPACITY) ||  
      ! init_lfht(&mmap_tbl, MMAP_HTABLE_CAPACITY)  ){
    fprintf(stderr, "Off to frollocing a bad start\n");
    abort();
  }
  __initialized__ = true;
  pthread_mutex_unlock(&lock);
  return;
}

/* not sure how we will ever call this */
void frolloc_delete(void)
{
  __initialized__ = false;
  delete_lfht(&desc_tbl);
  delete_lfht(&mmap_tbl);
}

/* 
   Since SuperBlocks are aligned on SBSIZE boundaries, we use Gloger's
   technique (arena.c in ptmalloc or glibc malloc) to lookup the descriptor 
   associated with a arbitrary client pointer.
*/
static descriptor* pointer2Descriptor(void *ptr)
{
  uintptr_t val = 0, key;
  bool success;
  
  key = ((uintptr_t)ptr & ~(SBSIZE - 1));
  success = lfht_find(&desc_tbl, key, &val);
  return success ? (descriptor*)val : NULL;
}

static bool is_mmapped(void *ptr, size_t* szp)
{
  uintptr_t val;
  bool success;

  success = lfht_find(&mmap_tbl, ( uintptr_t)ptr, &val);

  if(success){
    if( szp != NULL ){ *szp = val; }
  }
  //note bene: having val TOMBSTONE means it has been freed and could now be
  //part of a super block!
  return val == TOMBSTONE ? false : success;
}

static void* AllocNewSB(size_t size, unsigned long alignment)
{
  void* addr;
  
  addr = aligned_mmap(size, alignment);

  assert( alignment == 0 || ((uintptr_t)addr & ((uintptr_t)alignment - 1 )) == 0);

  if (addr == NULL) {
    fprintf(stderr, "AllocNewSB() mmap of size %lu returned NULL\n", size);
    fflush(stderr);
    abort();
  }
  
  return addr;
}


static void organize_desc_list(descriptor* start, unsigned long count, unsigned long stride)
{
  uintptr_t ptr;
  unsigned int i;
 
  start->Next = (descriptor*)(start + stride);
  ptr = (uintptr_t)start; 
  for (i = 1; i < count - 1; i++) {
    ptr += stride;
    ((descriptor*)ptr)->Next = (descriptor*)(ptr + stride);
  }
  ptr += stride;
  ((descriptor*)ptr)->Next = NULL;
}

static void organize_list(void* start, unsigned long count, unsigned long stride)
{
  uintptr_t ptr;
  unsigned long i;
  
  ptr = (uintptr_t)start; 
  for (i = 1; i < count - 1; i++) {
    ptr += stride;
    *((uintptr_t*)ptr) = i + 1;
  }
}

static descriptor* DescAlloc()
{
  descriptor_queue old_queue, new_queue;
  descriptor* desc;
  
  while(true) {

    old_queue = queue_head;

    
    if (old_queue.DescAvail) {

      //there is a descriptor in the queue; try and grab it.
      
      new_queue.DescAvail = (uintptr_t)((descriptor*)old_queue.DescAvail)->Next;
      new_queue.tag = old_queue.tag + 1;
      if (cas_128((volatile u128_t*)&queue_head, *((u128_t*)&old_queue), *((u128_t*)&new_queue))) {
	//we succeeded
        desc = (descriptor*)old_queue.DescAvail;
        break;
      }
      else {
	//we failed
	continue;
      }
    }
    else {

      // we need to allocate a new block of descriptors and install it in the queue.
      
      desc = aligned_mmap(DESCSBSIZE, 0);
      
      if (desc == NULL) {
	fprintf(stderr, "DescAlloc: aligned_mmap of size %lu returned NULL\n", DESCSBSIZE);
	fflush(stderr);
	abort();
      }

      organize_desc_list((void *)desc, DESCSBSIZE / sizeof(descriptor), sizeof(descriptor));

      new_queue.DescAvail = (uintptr_t)desc->Next;
      new_queue.tag = old_queue.tag + 1;
      if (cas_128((volatile u128_t*)&queue_head, *((u128_t*)&old_queue), *((u128_t*)&new_queue))) {
        break;
      }
      else {
	// someone beat us to it
	munmap((void*)desc, DESCSBSIZE);
	continue;
      }
    }
  }
  
  return desc;
}

/* lock free push of desc onto the front of the descriptor queue queue_head */
void DescRetire(descriptor* desc)
{
  descriptor_queue old_queue, new_queue;

  do {
    old_queue = queue_head;
    desc->Next = (descriptor*)old_queue.DescAvail;
    new_queue.DescAvail = (uintptr_t)desc;
    new_queue.tag = old_queue.tag + 1;

    /* maged michael has a memory fence here; and no ABA tag */
    
  } while (!cas_128((volatile u128_t*)&queue_head, 
		    *((u128_t*)&old_queue), 
		    *((u128_t*)&new_queue)));
}

static void ListRemoveEmptyDesc(sizeclass* sc)
{

#if 1
  descriptor *desc;
  lf_fifo_queue_t temp = LF_FIFO_QUEUE_STATIC_INIT;
  while (true) {
    desc = (descriptor *)lf_fifo_dequeue(&sc->Partial);
    if(desc == NULL){ break; }
    lf_fifo_enqueue(&temp, (void *)desc);
    if (desc->sb == NULL) {
      DescRetire(desc);
    }
    else {
      break;
    }
  }
  
  while (true) {
    desc = (descriptor *)lf_fifo_dequeue(&temp);
    if(desc == NULL){ break; }
    lf_fifo_enqueue(&sc->Partial, (void *)desc);
  }
#endif
}

static descriptor* ListGetPartial(sizeclass* sc)
{
  return (descriptor*)lf_fifo_dequeue(&sc->Partial);
}

static void ListPutPartial(descriptor* desc)
{
  lf_fifo_enqueue(&desc->heap->sc->Partial, (void*)desc);  
}

static void RemoveEmptyDesc(procheap* heap, descriptor* desc)
{
  if (cas_64((volatile uint64_t *)&heap->Partial, (uint64_t)desc, 0)) {
    DescRetire(desc);
  }
  else {
    ListRemoveEmptyDesc(heap->sc);
  }
}

static descriptor* HeapGetPartial(procheap* heap)
{ 
  descriptor* desc;
  
  do {
    desc = *((descriptor**)&heap->Partial); // casts away the volatile
    if (desc == NULL) {
      return ListGetPartial(heap->sc);
    }
  } while (!cas_64((volatile uint64_t *)&heap->Partial, ( uint64_t)desc, 0));

  return desc;
}

static void HeapPutPartial(descriptor* desc)
{
  descriptor* prev;

  do {
    prev = (descriptor*)desc->heap->Partial; // casts away volatile
  } while (!cas_64((volatile uint64_t *)&desc->heap->Partial, (uint64_t)prev, (uint64_t)desc));

  if (prev) {
    ListPutPartial(prev); 
  }
}

static void UpdateActive(procheap* heap, descriptor* desc, unsigned long morecredits)
{ 
  active oldactive, newactive;
  anchor oldanchor, newanchor;

  oldactive.ptr =  0;
  oldactive.credits = 0;

  newactive.ptr = (uintptr_t)desc;
  newactive.credits = morecredits - 1;

  if (cas_128((volatile u128_t *)&heap->Active,
	      *((u128_t*)&oldactive), 
	      *((u128_t*)&newactive))) {
    return;
  }

  // Someone installed another active sb
  // Return credits to sb and make it partial
  do { 
    newanchor = oldanchor = desc->Anchor;
    newanchor.count += morecredits;
    newanchor.state = PARTIAL;
  } while (!cas_64((volatile uint64_t *)&desc->Anchor, 
		   *((uint64_t*)&oldanchor), 
		   *((uint64_t*)&newanchor)));

  HeapPutPartial(desc);
}

static descriptor* mask_credits(active oldactive)
{
  return (descriptor*)oldactive.ptr;
}

static void* MallocFromActive(procheap *heap) 
{
  active newactive, oldactive;
  descriptor* desc;
  anchor oldanchor, newanchor;
  void* addr;
  unsigned long morecredits = 0;
  unsigned int next = 0;

  // First step: reserve block
  do { 
    newactive = oldactive = heap->Active;
    if (!(*((unsigned long long*)(&oldactive)))) {   
      //sri: we split active and credits, so we do not need to do this
      return NULL;
    }
    if (oldactive.credits == 0) {
      *((unsigned long long*)(&newactive)) = 0;  
      //sri: we split active and credits, so we do not need to do this
    }
    else {
      --newactive.credits;
    }
  } while (!cas_128((volatile u128_t*)&heap->Active, 
		    *((u128_t*)&oldactive), 
		    *((u128_t*)&newactive)));
  

  // Second step: pop block
  desc = mask_credits(oldactive);
  do {
    // state may be ACTIVE, PARTIAL or FULL
    newanchor = oldanchor = desc->Anchor;
    addr = (void *)((unsigned long)desc->sb + oldanchor.avail * desc->sz);
    next = *(unsigned long *)addr;
    newanchor.avail = next; 
    ++newanchor.tag;

    if (oldactive.credits == 0) {

      // state must be ACTIVE
      if (oldanchor.count == 0) {
        newanchor.state = FULL;
      }
      else { 
        morecredits = min(oldanchor.count, MAXCREDITS);
        newanchor.count -= morecredits;
      }
    } 
  } while (!cas_64((volatile uint64_t*)&desc->Anchor, 
		   *((uint64_t*)&oldanchor), 
		   *((uint64_t*)&newanchor)));

  if (oldactive.credits == 0 && oldanchor.count > 0) {
    UpdateActive(heap, desc, morecredits);
  }

  return addr;
}

static void* MallocFromPartial(procheap* heap)
{
  descriptor* desc;
  anchor oldanchor, newanchor;
  unsigned long morecredits;
  void* addr;
  
 retry:
  desc = HeapGetPartial(heap);
  if (!desc) {
    return NULL;
  }

  desc->heap = heap;
  do {
    // reserve blocks
    newanchor = oldanchor = desc->Anchor;
    if (oldanchor.state == EMPTY) {

      //iam: added this in the hope that it is now true...
      assert(desc->sb == NULL);
      
      DescRetire(desc); 
      goto retry;
    }

    // oldanchor state must be PARTIAL
    // oldanchor count must be > 0
    morecredits = min(oldanchor.count - 1, MAXCREDITS);
    newanchor.count -= morecredits + 1;
    newanchor.state = (morecredits > 0) ? ACTIVE : FULL;
  } while (!cas_64((volatile uint64_t*)&desc->Anchor, 
		   *((uint64_t*)&oldanchor), 
		   *((uint64_t*)&newanchor)));

  do { 
    // pop reserved block
    newanchor = oldanchor = desc->Anchor;
    addr = (void*)((unsigned long)desc->sb + oldanchor.avail * desc->sz);

    newanchor.avail = *(unsigned long*)addr;
    ++newanchor.tag;
  } while (!cas_64((volatile uint64_t*)&desc->Anchor, 
		   *((uint64_t*)&oldanchor), 
		   *((uint64_t*)&newanchor)));

  if (morecredits > 0) {
    UpdateActive(heap, desc, morecredits);
  }

  return addr;
}

static void* MallocFromNewSB(procheap* heap, descriptor** descp)
{
  descriptor* desc;
  void* addr;
  active newactive, oldactive;

  assert(descp != NULL);

  oldactive.ptr = 0;
  oldactive.credits = 0;
  
  desc = DescAlloc();
  desc->sb = AllocNewSB(heap->sc->sbsize, SBSIZE);

  desc->heap = heap;
  desc->Anchor.avail = 1;
  desc->sz = heap->sc->sz;
  desc->maxcount = heap->sc->sbsize / desc->sz;

  // Organize blocks in a linked list starting with index 0.
  organize_list(desc->sb, desc->maxcount, desc->sz);

  *((unsigned long long*)&newactive) = 0;
  newactive.ptr = (unsigned long)desc;
  newactive.credits = min(desc->maxcount - 1, MAXCREDITS) - 1;

  desc->Anchor.count = max(((signed long)desc->maxcount - 1 ) - ((signed long)newactive.credits + 1), 0); // max added by Scott
  desc->Anchor.state = ACTIVE;

  // memory fence.
  if (cas_128((volatile u128_t*)&heap->Active, 
	      *((u128_t*)&oldactive), 
	      *((u128_t*)&newactive))) { 
    addr = desc->sb;
    // pass out the new descriptor
    *descp = desc;
    return addr;

  } 
  else {
    //Free the superblock desc->sb.
    munmap(desc->sb, desc->heap->sc->sbsize);
    //iam suggests:
    desc->sb = NULL;
    DescRetire(desc); 
    return NULL;
  }
}

static procheap* find_heap(size_t sz)
{
  procheap* heap;

  if (sz >= MAX_BLOCK_SIZE) {
    return NULL;
  }
  
  heap = heaps[sz / GRANULARITY];
  if (heap == NULL) {
    heap = mmap(NULL, sizeof(procheap), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    *((unsigned long long*)&(heap->Active)) = 0;
    heap->Partial = NULL;
    heap->sc = &sizeclasses[sz / GRANULARITY];
    heaps[sz / GRANULARITY] = heap;
  }
        
  return heap;
}

static void* alloc_large_block(size_t sz)
{
  void* addr;
  bool success;

  sz = align_up(sz, GRANULARITY);

  addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  //sri: remember that we can fail ...
  if(addr == NULL){
    return addr;
  } else {
    success = lfht_insert_or_update(&mmap_tbl, (uintptr_t)addr, (uintptr_t)sz);
    if( ! success ){
      fprintf(stderr, "malloc() mmap table full\n");
      fflush(stderr);
      abort();
    }
    return addr;
  }
}


void* malloc(size_t sz)
{ 
  procheap *heap;
  void* addr;
  descriptor* desc = NULL;
  
  if (! __initialized__ ){ frolloc_init();  }


  //minimum block size
  if (sz < 16){
    sz = 16;
  }

  // Use sz and thread id to find heap.
  heap = find_heap(sz);

  if (!heap) {
    // Large block (sri: unless the mmap fails)
    addr = alloc_large_block(sz);
    log_malloc(addr, sz);
    return addr;
  }

  while(1) { 
    addr = MallocFromActive(heap);
    if (addr) {
      log_malloc(addr, sz);
      return addr;
    }
    addr = MallocFromPartial(heap);
    if (addr) {
      log_malloc(addr, sz);
      return addr;
    }
    addr = MallocFromNewSB(heap, &desc);

    if(desc){
      //note bene: we could be recycling a super block!
      bool success = lfht_insert_or_update(&desc_tbl, (uintptr_t)desc->sb, (uintptr_t)desc);
      if( ! success ){
	fprintf(stderr, "malloc() descriptor table full\n");
	fflush(stderr);
	abort();
      }
    }

    if (addr) {
      log_malloc(addr, sz);
      return addr;
    }
  }
  
}

void *calloc(size_t nmemb, size_t size)
{
  void *ptr;
        
  ptr = malloc(nmemb*size);

  if ( ptr != NULL) {
    memset(ptr, 0, nmemb*size);
  }
  
  return ptr;
}

void *memalign(size_t boundary, size_t size)
{
  void *p;

  p = malloc((size + boundary - 1) & ~(boundary - 1));
  if (!p) {
    return NULL;
  }

  return(void*)(((unsigned long)p + boundary - 1) & ~(boundary - 1)); 
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
  *memptr = memalign(alignment, size);
  if (*memptr) {
    return 0;
  }
  else {
    /* We have to "personalize" the return value according to the error */
    return -1;
  }
}

/* 
   free the pointer ptr from the superblock desc->sb that it belongs to.
   called from free and realloc.
 */
void free_from_sb(void* ptr, descriptor* desc){
  anchor oldanchor, newanchor;
  procheap* heap = NULL;
  void *sb;

  assert(desc != NULL);

  sb = desc->sb;
  do { 
    newanchor = oldanchor = desc->Anchor;
    
    *((unsigned long*)ptr) = oldanchor.avail;
    newanchor.avail = ((unsigned long)ptr - (unsigned long)sb) / desc->sz;
    
    if (oldanchor.state == FULL) {
      newanchor.state = PARTIAL;
    }
    
    if (oldanchor.count == desc->maxcount - 1) {
      heap = desc->heap;
      // instruction fence.
      newanchor.state = EMPTY;
    } 
    else {
      ++newanchor.count;
    }
    // memory fence.
  } while (!cas_64((volatile uint64_t*)&desc->Anchor, 
		   *((uint64_t*)&oldanchor), 
		   *((uint64_t*)&newanchor)));
  
  if (newanchor.state == EMPTY) {
    /* it is important to update the table prior to giving back the
       memory to the operating system. since it can be very quick
       it putting the addresses back into play.
    */
    bool success = lfht_update(&desc_tbl, (uintptr_t)sb, TOMBSTONE);
    if( ! success ){
      fprintf(stderr, "free() desc table update failed\n");
      fflush(stderr);
    }    
    munmap(sb, heap->sc->sbsize);
    //iam suggests:
    desc->sb = NULL;
    RemoveEmptyDesc(heap, desc);
  } 
  else if (oldanchor.state == FULL) {
    HeapPutPartial(desc);
  }
}


void free(void* ptr) 
{
  descriptor* desc;
  bool success;
  size_t sz;

  log_free(ptr);
  
  if (!ptr) {
    return;
  }
  
  if(is_mmapped(ptr, &sz)){
    /* it is important to update the table prior to giving back the
       memory to the operating system. since it can be very quick
       it putting the addresses back into play.
    */
    success = lfht_update(&mmap_tbl, (uintptr_t)ptr, TOMBSTONE);
    if( ! success ){
      fprintf(stderr, "free(): mmap table update failed\n");
      fflush(stderr);
    }    
    munmap(ptr, sz);
    return;
  }
  else {
    
    desc = pointer2Descriptor(ptr);

    if( desc != NULL ){

      free_from_sb(ptr, desc);

    } else {
      fprintf(stderr, "free(%p) ferrel pointer ignoring. desc = '%p'\n", ptr, desc);
      fflush(stderr);
    }
  }
}

void *realloc(void *object, size_t size)
{
  descriptor* desc;
  void* ret;
  size_t minsize;
  bool success;
  size_t sz;
  void* newobject;

  if (object == NULL) {
    newobject = malloc(size);
    return newobject;
  }
  else if (size == 0) {
    free(object);
    return NULL;
  }

  if(is_mmapped(object, &sz)){
    ret = malloc(size);
    minsize = sz;
    /* note that we could be getting smaller NOT larger */
    if(sz > size){
      minsize = size;
    }
    memcpy(ret, object, minsize);
    /* 
       it is important to update the table prior to giving back the
       memory to the operating system. since it can be very quick
       it putting the addresses back into play.
    */
    success = lfht_update(&mmap_tbl, (uintptr_t)object, TOMBSTONE);
    if( ! success ){
      fprintf(stderr, "realloc() mmap table update failed\n");
      fflush(stderr);
    }    
    munmap(object, sz);
    log_free(object);
  }
  else {
    desc = pointer2Descriptor(object);
    if (desc == NULL){
      /* not much we can do here; but fail */
      sz = -1;
      is_mmapped(object, &sz);
      fprintf(stderr, "realloc(%p) in %p has no meta data!  (sz in mmap table = %d)\n", 
	      object, (void *)pthread_self(), (int)sz);
      fflush(stderr);
      log_realloc(NULL, object, size);
      log_end();
      exit(1);
    } else if (size <= desc->sz ) {
      ret = object;
    }
    else {
      ret = malloc(size);
      memcpy(ret, object, desc->sz);
      free_from_sb(object, desc); 
      log_free(object);
    }
  }
  
  return ret;
}


void malloc_stats(void){
  fprintf(stderr, "malloc_stats coming soon(ish)\n");
}


