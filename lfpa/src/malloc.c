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
#include <stdbool.h>

#include "malloc.h"
#include "malloc_internals.h"


static __thread procheap* heaps[MAX_BLOCK_SIZE / GRANULARITY] =  { };

static volatile descriptor_queue queue_head;

static void* AllocNewSB(size_t size, unsigned long alignment)
{
  void* addr;
  
  addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (addr == MAP_FAILED) {
    fprintf(stderr, "AllocNewSB() mmap failed, %lu, tag %"PRIu64": ", size, queue_head.tag);
    switch (errno) {
    case EBADF:         fprintf(stderr, "EBADF"); break;
    case EACCES:        fprintf(stderr, "EACCES"); break;
    case EINVAL:        fprintf(stderr, "EINVAL"); break;
    case ETXTBSY:       fprintf(stderr, "ETXBSY"); break;
    case EAGAIN:        fprintf(stderr, "EAGAIN"); break;
    case ENOMEM:        fprintf(stderr, "ENOMEM"); break;
    case ENODEV:        fprintf(stderr, "ENODEV"); break;
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(1);
  }
  else if (addr == NULL) {
    fprintf(stderr, "AllocNewSB() mmap of size %lu returned NULL, tag %"PRIu64"\n", size, queue_head.tag);
    fflush(stderr);
    exit(1);
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
  
#ifdef DEBUG
  fprintf(stderr, "In DescAlloc\n");
  fflush(stderr);
#endif

  while(1) {
    old_queue = queue_head;
    if (old_queue.DescAvail) {
      new_queue.DescAvail = (uintptr_t)((descriptor*)old_queue.DescAvail)->Next;
      new_queue.tag = old_queue.tag + 1;
      if (compare_and_swap128((volatile aba_128_t*)&queue_head, *((aba_128_t*)&old_queue), *((aba_128_t*)&new_queue))) {
        desc = (descriptor*)old_queue.DescAvail;
#ifdef DEBUG
        fprintf(stderr, "Returning recycled descriptor %p (tag %"PRIu64")\n", desc, queue_head.tag);
        fflush(stderr);
#endif
        break;
      }
    }
    else {
      desc = AllocNewSB(DESCSBSIZE, sizeof(descriptor));
      organize_desc_list((void *)desc, DESCSBSIZE / sizeof(descriptor), sizeof(descriptor));

      new_queue.DescAvail = (uintptr_t)desc->Next;
      new_queue.tag = old_queue.tag + 1;
      if (compare_and_swap128((volatile aba_128_t*)&queue_head,
			      *((aba_128_t*)&old_queue),
			      *((aba_128_t*)&new_queue))) {
#ifdef DEBUG
        fprintf(stderr, "Returning descriptor %p from new descriptor block\n", desc);
        fflush(stderr);
#endif
        break;
      }
      else {
	// someone beat us to it
	munmap((void*)desc, DESCSBSIZE);
      }
    }
  }

  return desc;
}

void DescRetire(descriptor* desc)
{
  descriptor_queue old_queue, new_queue;

#ifdef DEBUG
  fprintf(stderr, "Recycling descriptor %p (sb %p, tag %hu)\n", desc, desc->sb, queue_head.tag);
  fflush(stderr);
#endif  
  do {
    old_queue = queue_head;
    desc->Next = (descriptor*)old_queue.DescAvail;
    new_queue.DescAvail = (uintptr_t)desc;
    new_queue.tag = old_queue.tag + 1;
  } while (!compare_and_swap128((volatile aba_128_t*)&queue_head, *((aba_128_t*)&old_queue), *((aba_128_t*)&new_queue)));
}

static void ListRemoveEmptyDesc(sizeclass* sc)
{
#if 1
  //gotta figure out why this is broken
  descriptor *desc;
  lf_fifo_queue_t temp = LF_FIFO_QUEUE_STATIC_INIT;

  while (true) {
    desc = (descriptor *)lf_fifo_dequeue(&sc->Partial);
    if(desc == NULL){ break; }
    if (desc->sb == NULL) {
#ifdef DEBUG
      fprintf(stderr, "ListRemoveEmptyDesc(%u) retired one!\n", sc->sz);
      fflush(stderr);
#endif  
      DescRetire(desc);
      break;
    }
    lf_fifo_enqueue(&temp, (void *)desc);
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
  if (compare_and_swap_ptr(&heap->Partial, desc, NULL)) {
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
  } while (!compare_and_swap_ptr(&heap->Partial, desc, NULL));

  return desc;
}

static void HeapPutPartial(descriptor* desc)
{
  descriptor* prev;

  do {
    prev = (descriptor*)desc->heap->Partial; // casts away volatile
  } while (!compare_and_swap_ptr(&desc->heap->Partial, prev, desc));

  if (prev) {
    ListPutPartial(prev); 
  }
}

static void UpdateActive(procheap* heap, descriptor* desc, unsigned long morecredits)
{ 
  active oldactive, newactive;
  anchor oldanchor, newanchor;

#ifdef DEBUG
  fprintf(stderr, "UpdateActive() heap->Active %p, credits %lu\n", *((void**)&heap->Active), morecredits);
  fflush(stderr);
#endif

  //*((unsigned long long*)&oldactive) = 0;
  oldactive.ptr =  0;
  oldactive.credits = 0;
  newactive.ptr = (uintptr_t)desc;
  newactive.credits = morecredits - 1;
  if (compare_and_swap128((volatile aba_128_t *)&heap->Active, *((aba_128_t*)&oldactive), *((aba_128_t*)&newactive))) {
    return;
  }

  // Someone installed another active sb
  // Return credits to sb and make it partial
  do { 
    newanchor = oldanchor = desc->Anchor;
    newanchor.count += morecredits;
    newanchor.state = PARTIAL;
  } while (!compare_and_swap64((volatile uintptr_t *)&desc->Anchor, *((uintptr_t*)&oldanchor), *((uintptr_t*)&newanchor)));

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
    if (!(*((unsigned long long*)(&oldactive)))) {   //sri: we split active and credits, so we do not need to do this
      return NULL;
    }
    if (oldactive.credits == 0) {
      *((unsigned long long*)(&newactive)) = 0;  //sri: we split active and credits, so we do not need to do this
#ifdef DEBUG
      fprintf(stderr, "MallocFromActive() setting active to NULL, %lu, %d\n", newactive.ptr, newactive.credits);
      fflush(stderr);
#endif
    }
    else {
      --newactive.credits;
    }
  } while (!compare_and_swap128((volatile aba_128_t*)&heap->Active, *((aba_128_t*)&oldactive), *((aba_128_t*)&newactive)));

#ifdef DEBUG
  fprintf(stderr, "MallocFromActive() heap->Active %p, credits %hu\n", *((void**)&heap->Active), oldactive.credits);
  fflush(stderr);
#endif

  // Second step: pop block
  desc = mask_credits(oldactive);
  do {
    // state may be ACTIVE, PARTIAL or FULL
    newanchor = oldanchor = desc->Anchor;
    assert(desc->sb != (void*)(uintptr_t)0xdeadbeef && desc->sb != (void*)(uintptr_t)0xcafebabe);
    addr = (void *)((unsigned long)desc->sb + oldanchor.avail * desc->sz);
    next = *(unsigned long *)addr;
    newanchor.avail = next; //sri: shenanigans?
    ++newanchor.tag;

    if (oldactive.credits == 0) {

      // state must be ACTIVE
      if (oldanchor.count == 0) {
#ifdef DEBUG
        fprintf(stderr, "MallocFromActive() setting superblock %p to FULL\n", desc->sb);
        fflush(stderr);
#endif
        newanchor.state = FULL;
      }
      else { 
        morecredits = min(oldanchor.count, MAXCREDITS);
        newanchor.count -= morecredits;
      }
    } 
  } while (!compare_and_swap64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

#ifdef DEBUG
  fprintf(stderr, "MallocFromActive() sb %p, Active %p, avail %d, oldanchor.count %hu, newanchor.count %hu, morecredits %lu, MAX %d\n", 
          desc->sb, *((void**)&heap->Active), desc->Anchor.avail, oldanchor.count, newanchor.count, morecredits, MAXCREDITS);
  fflush(stderr);
#endif

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

      //iam: added this in the hope that it is now true...
      //      assert(desc->sb == NULL); 

      DescRetire(desc); 
      goto retry;
    }

    // oldanchor state must be PARTIAL
    // oldanchor count must be > 0
    morecredits = min(oldanchor.count - 1, MAXCREDITS);
    newanchor.count -= morecredits + 1;
    newanchor.state = (morecredits > 0) ? ACTIVE : FULL;
  } while (!compare_and_swap64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  do { 
    // pop reserved block
    newanchor = oldanchor = desc->Anchor;
    addr = (void*)((unsigned long)desc->sb + oldanchor.avail * desc->sz);

    newanchor.avail = *(unsigned long*)addr;
    ++newanchor.tag;
  } while (!compare_and_swap64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  if (morecredits > 0) {
    UpdateActive(heap, desc, morecredits);
  }

  *((char*)addr) = (char)SMALL;   //sri: not seeing a use of this
  addr += TYPE_SIZE;
  *((descriptor**)addr) = desc; 
  return ((void *)((unsigned long)addr + PTR_SIZE));
}

static void* MallocFromNewSB(procheap* heap)
{
  descriptor* desc;
  void* addr;
  active newactive, oldactive;

  // *((unsigned long long*)&oldactive) = 0;
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

#ifdef DEBUG
  fprintf(stderr, "New SB %p associated with desc %p (sz %u, sbsize %d, heap %p, Anchor.avail %hu, Anchor.count %hu)\n", 
          desc->sb, desc, desc->sz, heap->sc->sbsize, heap, desc->Anchor.avail, desc->Anchor.count);
  fflush(stderr);
#endif

  *((unsigned long long*)&newactive) = 0;
  newactive.ptr = (unsigned long)desc;
  newactive.credits = min(desc->maxcount - 1, MAXCREDITS) - 1;

  desc->Anchor.count = max(((signed long)desc->maxcount - 1 ) - ((signed long)newactive.credits + 1), 0); // max added by Scott
  desc->Anchor.state = ACTIVE;

#ifdef DEBUG
  fprintf(stderr, "MallocFromNewSB() sz %u, maxcount %u, Anchor.count %hu, newactive.credits %hu, max %ld\n", 
          desc->sz, desc->maxcount, desc->Anchor.count, newactive.credits, 
          ((signed long)desc->maxcount - 1 ) - ((signed long)newactive.credits + 1));
  fflush(stderr);
#endif

  // memory fence.
  if (compare_and_swap128((volatile aba_128_t*)&heap->Active, *((aba_128_t*)&oldactive), *((aba_128_t*)&newactive))) { 
    addr = desc->sb;
    assert(desc->sb != (void*)(uintptr_t)0xdeadbeef && desc->sb != (void*)(uintptr_t)0xcafebabe);    
    *((char*)addr) = (char)SMALL;   //sri: not seeing a use of this
    addr += TYPE_SIZE;
    *((descriptor **)addr) = desc; 
    return (void *)((unsigned long)addr + PTR_SIZE);
  } 
  else {
    //Free the superblock desc->sb.
    munmap(desc->sb, desc->heap->sc->sbsize);
    //iam suggests:
    //    desc->sb = NULL;
    desc->sb = (void*)(uintptr_t)0xdeadbeef;
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
  addr = mmap(NULL, sz + HEADER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  // If the highest bit of the descriptor is 1, then the object is large (allocated / freed directly from / to the OS)
  *((char*)addr) = (char)LARGE;
  addr += TYPE_SIZE;
  *((unsigned long *)addr) = sz + HEADER_SIZE;
  return (void*)(addr + PTR_SIZE); 
}

#ifndef USE_LFPA_PREFIX
void* malloc(size_t sz)
#else
void* lfpa_malloc(size_t sz)
#endif
{ 
  procheap *heap;
  void* addr;
  
  //minimum block size
  if (sz < 16){
    sz = 16;
  }

#ifdef DEBUG
  fprintf(stderr, "malloc() sz %lu\n", sz);
  fflush(stderr);
#endif
  // Use sz and thread id to find heap.
  heap = find_heap(sz);

  if (!heap) {
    // Large block (sri: unless the mmap fails)
    addr = alloc_large_block(sz);
#ifdef DEBUG
    fprintf(stderr, "Large block allocation: %p\n", addr);
    fflush(stderr);
#endif
    return addr;
  }

  while(1) { 
    addr = MallocFromActive(heap);
    if (addr) {
#ifdef DEBUG
      fprintf(stderr, "malloc() return MallocFromActive %p\n", addr);
      fflush(stderr);
#endif
      return addr;
    }
    addr = MallocFromPartial(heap);
    if (addr) {
#ifdef DEBUG
      fprintf(stderr, "malloc() return MallocFromPartial %p\n", addr);
      fflush(stderr);
#endif
      return addr;
    }
    addr = MallocFromNewSB(heap);

    if (addr) {
#ifdef DEBUG
      fprintf(stderr, "malloc() return MallocFromNewSB %p\n", addr);
      fflush(stderr);
#endif
      return addr;
    }
  } 
}


#ifndef USE_LFPA_PREFIX
void *calloc(size_t nmemb, size_t size)
#else
void *lfpa_calloc(size_t nmemb, size_t size)
#endif
{
  void *ptr;
        
  ptr = malloc(nmemb*size);
  if (!ptr) {
    return NULL;
  }

  return memset(ptr, 0, nmemb*size);
}

#ifndef USE_LFPA_PREFIX
void *memalign(size_t boundary, size_t size)
#else
void *lfpa_memalign(size_t boundary, size_t size)
#endif
{
  void *p;

  p = malloc((size + boundary - 1) & ~(boundary - 1));
  if (!p) {
    return NULL;
  }

  return(void*)(((unsigned long)p + boundary - 1) & ~(boundary - 1)); 
}

#ifndef USE_LFPA_PREFIX
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
#else
int lfpa_posix_memalign(void **memptr, size_t alignment, size_t size)
{
  *memptr = lfpa_memalign(alignment, size);
  if (*memptr) {
    return 0;
  }
  else {
    /* We have to "personalize" the return value according to the error */
    return -1;
  }
}
#endif



#ifndef USE_LFPA_PREFIX
void free(void* ptr) 
#else
void lfpa_free(void* ptr) 
#endif
{
  descriptor* desc;
  void* sb;
  anchor oldanchor, newanchor;
  procheap* heap = NULL;

#ifdef DEBUG
  fprintf(stderr, "Calling my free %p\n", ptr);
  fflush(stderr);
#endif

  if (!ptr) {
    return;
  }
        
  // get prefix
  ptr = (void*)((unsigned long)ptr - HEADER_SIZE);  
  if (*((char*)ptr) == (char)LARGE) {
#ifdef DEBUG
    fprintf(stderr, "Freeing large block\n");
    fflush(stderr);
#endif
    munmap(ptr, *((unsigned long *)(ptr + TYPE_SIZE)));
    return;
  }
  desc = *((descriptor**)((unsigned long)ptr + TYPE_SIZE));
        
  sb = desc->sb;


  do { 
    newanchor = oldanchor = desc->Anchor;

    *((unsigned long*)ptr) = oldanchor.avail;
    newanchor.avail = ((unsigned long)ptr - (unsigned long)sb) / desc->sz;

    if (oldanchor.state == FULL) {
#ifdef DEBUG
      fprintf(stderr, "Marking superblock %p as PARTIAL\n", sb);
      fflush(stderr);
#endif
      newanchor.state = PARTIAL;
    }

    if (oldanchor.count == desc->maxcount - 1) {
      heap = desc->heap;
      // instruction fence.
#ifdef DEBUG
      fprintf(stderr, "Marking superblock %p as EMPTY; count %d\n", sb, oldanchor.count);
      fflush(stderr);
#endif
      newanchor.state = EMPTY;
    } 
    else {
      ++newanchor.count;
    }
    // memory fence.
  } while (!compare_and_swap64((volatile unsigned long*)&desc->Anchor, *((unsigned long*)&oldanchor), *((unsigned long*)&newanchor)));

  if (newanchor.state == EMPTY) {
#ifdef DEBUG
    fprintf(stderr, "Freeing superblock %p with desc %p (count %hu)\n", sb, desc, desc->Anchor.count);
    fflush(stderr);
#endif

    /*
    if(true){
      fprintf(stderr, "Freeing a sb\n");
      abort();
    }
    */

    munmap(sb, heap->sc->sbsize);
    //iam suggests:
    //    desc->sb = NULL;
    desc->sb = (void*)(uintptr_t)0xcafebabe;
    RemoveEmptyDesc(heap, desc);
  } 
  else if (oldanchor.state == FULL) {
#ifdef DEBUG
    fprintf(stderr, "Puting superblock %p to PARTIAL heap\n", sb);
    fflush(stderr);
#endif
    HeapPutPartial(desc);
  }
}

#ifndef USE_LFPA_PREFIX
void *realloc(void *object, size_t size)
#else
void *lfpa_realloc(void *object, size_t size)
#endif
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
    size_t sz = *((unsigned long *)(header + TYPE_SIZE));
    // the size in the header is the size of the entire mmap region
    // (i.e. client sz + HEADER_SIZE), when we copy below we will
    // only be copying the client memory, not the header.
    osize = sz - HEADER_SIZE;
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


#ifndef USE_LFPA_PREFIX
#else
#endif
void malloc_stats(void){
   fprintf(stderr, "malloc_stats coming soon(ish)\n");
}


