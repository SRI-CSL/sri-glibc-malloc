/*
 * Copyright (C) 2007  Scott Schneider, Christos Antonopoulos
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

#define TOMBSTONE 0

static sizeclass sizeclasses[MAX_BLOCK_SIZE / GRANULARITY];

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

#define HTABLE_CAPACITY 16*4096

/* not sure how we will ever call this */
void frolloc_delete(void)
{
  __initialized__ = false;
  delete_lfht(&desc_tbl);
  delete_lfht(&mmap_tbl);
}


static __thread procheap* heaps[MAX_BLOCK_SIZE / GRANULARITY] =  { };

static volatile descriptor_queue queue_head;

static void init_sizeclasses(void){
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
  init_sizeclasses();
  if( ! init_lfht(&desc_tbl, HTABLE_CAPACITY) ||  
      ! init_lfht(&mmap_tbl, HTABLE_CAPACITY)  ){
    fprintf(stderr, "Off to frollocing a bad start\n");
    abort();
  }
  __initialized__ = true;
  pthread_mutex_unlock(&lock);
  return;
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

static descriptor* DescAlloc() {
  
  descriptor_queue old_queue, new_queue;
  descriptor* desc;
  
  while(1) {
    old_queue = queue_head;
    if (old_queue.DescAvail) {
      new_queue.DescAvail = (uintptr_t)((descriptor*)old_queue.DescAvail)->Next;
      new_queue.tag = old_queue.tag + 1;
      if (cas_128((volatile u128_t*)&queue_head, *((u128_t*)&old_queue), *((u128_t*)&new_queue))) {
        desc = (descriptor*)old_queue.DescAvail;
        break;
      }
    }
    else {
      desc = AllocNewSB(DESCSBSIZE, 0); //no alignment needed

      organize_desc_list((void *)desc, DESCSBSIZE / sizeof(descriptor), sizeof(descriptor));

      new_queue.DescAvail = (uintptr_t)desc->Next;
      new_queue.tag = old_queue.tag + 1;
      if (cas_128((volatile u128_t*)&queue_head, *((u128_t*)&old_queue), *((u128_t*)&new_queue))) {
        break;
      }
      munmap((void*)desc, DESCSBSIZE);   
    }
  }

  return desc;
}

void DescRetire(descriptor* desc)
{
  descriptor_queue old_queue, new_queue;

  do {
    old_queue = queue_head;
    desc->Next = (descriptor*)old_queue.DescAvail;
    new_queue.DescAvail = (uintptr_t)desc;
    new_queue.tag = old_queue.tag + 1;
  } while (!cas_128((volatile u128_t*)&queue_head, *((u128_t*)&old_queue), *((u128_t*)&new_queue)));
}

static void ListRemoveEmptyDesc(sizeclass* sc)
{
  /*
    descriptor *desc;
    lf_fifo_queue_t temp = LF_FIFO_QUEUE_STATIC_INIT;

    while (desc = (descriptor *)lf_fifo_dequeue(&sc->Partial)) {
    lf_fifo_enqueue(&temp, (void *)desc);
    if (desc->sb == NULL) {
    DescRetire(desc);
    }
    else {
    break;
    }
    }

    while (desc = (descriptor *)lf_fifo_dequeue(&temp)) {
    lf_fifo_enqueue(&sc->Partial, (void *)desc);
    }
  */
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

  if (cas_128((volatile u128_t *)&heap->Active, *((u128_t*)&oldactive), *((u128_t*)&newactive))) {
    return;
  }

  // Someone installed another active sb
  // Return credits to sb and make it partial
  do { 
    newanchor = oldanchor = desc->Anchor;
    newanchor.count += morecredits;
    newanchor.state = PARTIAL;
  } while (!cas_64((volatile uintptr_t *)&desc->Anchor, *((uintptr_t*)&oldanchor), *((uintptr_t*)&newanchor)));

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
  } while (!cas_128((volatile u128_t*)&heap->Active, *((u128_t*)&oldactive), *((u128_t*)&newactive)));
  

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
  } while (!cas_64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  if (oldactive.credits == 0 && oldanchor.count > 0) {
    UpdateActive(heap, desc, morecredits);
  }

  *((char*)addr) = (char)SMALL;  //sri: not seeing a use of this
  addr += TYPE_SIZE;
  *((descriptor**)addr) = desc; 
  return ((void*)((unsigned long)addr + PTR_SIZE));
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
      DescRetire(desc); 
      goto retry;
    }

    // oldanchor state must be PARTIAL
    // oldanchor count must be > 0
    morecredits = min(oldanchor.count - 1, MAXCREDITS);
    newanchor.count -= morecredits + 1;
    newanchor.state = (morecredits > 0) ? ACTIVE : FULL;
  } while (!cas_64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  do { 
    // pop reserved block
    newanchor = oldanchor = desc->Anchor;
    addr = (void*)((unsigned long)desc->sb + oldanchor.avail * desc->sz);

    newanchor.avail = *(unsigned long*)addr;
    ++newanchor.tag;
  } while (!cas_64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  if (morecredits > 0) {
    UpdateActive(heap, desc, morecredits);
  }

  *((char*)addr) = (char)SMALL;   //sri: not seeing a use of this
  addr += TYPE_SIZE;
  *((descriptor**)addr) = desc; 
  return ((void *)((unsigned long)addr + PTR_SIZE));
}

static void* MallocFromNewSB(procheap* heap, descriptor** descp)
{
  descriptor* desc;
  void* addr;
  active newactive, oldactive;

  assert(descp != NULL);

  // *((unsigned long long*)&oldactive) = 0;
  oldactive.ptr = 0;
  oldactive.credits = 0;
  
  desc = DescAlloc();
  desc->sb = AllocNewSB(heap->sc->sbsize, SBSIZE);
  //desc->sb = AllocNewSB(SBSIZE, SBSIZE);

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
  if (cas_128((volatile u128_t*)&heap->Active, *((u128_t*)&oldactive), *((u128_t*)&newactive))) { 
    addr = desc->sb;
    *((char*)addr) = (char)SMALL;   //sri: not seeing a use of this
    addr += TYPE_SIZE;
    *((descriptor **)addr) = desc; 

    // pass out the new descriptor
    *descp = desc;

    return (void *)((unsigned long)addr + PTR_SIZE);
  } 
  else {
    //Free the superblock desc->sb.
    munmap(desc->sb, desc->heap->sc->sbsize);
    DescRetire(desc); 
    return NULL;
  }
}

static procheap* find_heap(size_t sz)
{
  procheap* heap;
  
  // We need to fit both the object and the descriptor in a single block
  sz += HEADER_SIZE;
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
  
  sz = align_up(sz, GRANULARITY);

  sz += HEADER_SIZE;

  addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  //sri: remember that we can fail ...
  if(addr == NULL){
    return addr;
  } else {
    void *ptr =  addr + HEADER_SIZE;
    bool success = lfht_insert_or_update(&mmap_tbl, (uintptr_t)ptr, (uintptr_t)sz);
    if( ! success ){
      fprintf(stderr, "malloc() mmap table full\n");
      fflush(stderr);
      abort();
    }

    // If the highest byte of the header is 0, 
    // then the object is large (allocated / freed directly from / to the OS)
    *((char*)addr) = (char)LARGE;
    addr += TYPE_SIZE;
    *((unsigned long *)addr) = sz;
    return (void*)(addr + PTR_SIZE); 
  }
}


void* malloc(size_t sz)
{ 
  procheap *heap;
  void* addr;
  descriptor* desc = NULL;
  
  if (! __initialized__){ frolloc_init();  }


  //minimum block size
  if (sz < 16){
    sz = 16;
  }

  // Use sz and thread id to find heap.
  heap = find_heap(sz);

  if (!heap) {
    // Large block (sri: unless the mmap fails)
    addr = alloc_large_block(sz);
    return addr;
  }

  while(1) { 
    addr = MallocFromActive(heap);
    if (addr) {
      return addr;
    }
    addr = MallocFromPartial(heap);
    if (addr) {
      return addr;
    }
    addr = MallocFromNewSB(heap, &desc);

    if(desc){
      bool success = lfht_insert(&desc_tbl, (uintptr_t)desc->sb, (uintptr_t)desc);
      if( ! success ){
	fprintf(stderr, "malloc() descriptor table full\n");
	fflush(stderr);
	abort();
      }
    }

    if (addr) {
      return addr;
    }
  } 
}


void *calloc(size_t nmemb, size_t size)
{
  void *ptr;
        
  ptr = malloc(nmemb*size);
  if (!ptr) {
    return NULL;
  }

  return memset(ptr, 0, nmemb*size);
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


void free(void* ptr) 
{
  descriptor* desc;
  void* sb;
  void *optr;
  anchor oldanchor, newanchor;
  procheap* heap = NULL;

  if (!ptr) {
    return;
  }
        
  optr = ptr;
  // get prefix
  ptr = (void*)((unsigned long)ptr - HEADER_SIZE);  
  if (*((char*)ptr) == (char)LARGE) {
    bool success;
#ifdef DEBUG
    fprintf(stderr, "Freeing large block\n");
    fflush(stderr);
#endif
    munmap(ptr, *((unsigned long *)(ptr + TYPE_SIZE)));
    success = lfht_update(&mmap_tbl, (uintptr_t)optr, TOMBSTONE);
    if( ! success ){
      fprintf(stderr, "malloc() mmap table update failed\n");
      fflush(stderr);
    }    
    return;
  }
  desc = *((descriptor**)((unsigned long)ptr + TYPE_SIZE));
        
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
  } while (!cas_64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  if (newanchor.state == EMPTY) {
    bool success = lfht_update(&desc_tbl, (uintptr_t)sb, TOMBSTONE);
    if( ! success ){
      fprintf(stderr, "malloc() desc table update failed\n");
      fflush(stderr);
    }    
    munmap(sb, heap->sc->sbsize);
    RemoveEmptyDesc(heap, desc);
  } 
  else if (oldanchor.state == FULL) {
    HeapPutPartial(desc);
  }
}

void *realloc(void *object, size_t size)
{
  descriptor* desc;
  void* header;
  void* ret;
  size_t osize;
  size_t minsize;

  if (object == NULL) {
    return malloc(size);
  }
  else if (size == 0) {
    free(object);
    return NULL;
  }

  header = (void*)((unsigned long)object - HEADER_SIZE);  

  if (*((char*)header) == (char)LARGE) {
    // the size in the header is the size of the entire mmap region
    // (i.e. client sz + HEADER_SIZE), when we copy below we will
    // only be copying the client memory, not the header.
    osize = *((unsigned long *)(header + TYPE_SIZE)) - HEADER_SIZE;
    ret = malloc(size);

    minsize = osize;
    /* note that we could be getting smaller NOT larger */
    if(osize > size){
      minsize = size;
    }

    memcpy(ret, object, minsize);
    munmap(object, osize);
    
  }
  else {
    desc = *((descriptor**)((unsigned long)header + TYPE_SIZE));
    if (size <= desc->sz - HEADER_SIZE) {
      ret = object;
    }
    else {
      ret = malloc(size);
      memcpy(ret, object, desc->sz - HEADER_SIZE);
      free(object);
    }
  }

  return ret;
}


void malloc_stats(void){
   fprintf(stderr, "malloc_stats coming soon(ish)\n");
}


