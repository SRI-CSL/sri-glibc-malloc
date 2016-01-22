/* Malloc implementation for multiple threads without lock contention.
   Copyright (C) 2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Wolfram Gloger <wg@malloc.de>, 2001.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* $Id: arena.c,v 1.9 2004/11/05 14:42:23 wg Exp $ */

/* Compile-time constants.  */

#define HEAP_MIN_SIZE (32*1024)
#ifndef HEAP_MAX_SIZE
#define HEAP_MAX_SIZE (1024*1024) /* must be a power of two */
#endif

/* HEAP_MIN_SIZE and HEAP_MAX_SIZE limit the size of mmap()ed heaps
   that are dynamically created for multi-threaded programs.  The
   maximum size must be a power of two, for fast determination of
   which heap belongs to a chunk.  It should be much larger than the
   mmap threshold, so that requests with a size just below that
   threshold can be fulfilled without creating too many heaps.  */


#ifndef THREAD_STATS
#define THREAD_STATS 0
#endif

/* If THREAD_STATS is non-zero, some statistics on mutex locking are
   computed.  */

/***************************************************************************/

/* A heap is a single contiguous memory region holding (coalesceable)
   malloc_chunks.  It is allocated with mmap() and always starts at an
   address aligned to HEAP_MAX_SIZE.  Not used unless compiling with
   USE_ARENAS. */

typedef struct _heap_info {
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t pad;    /* Make sure the following data is properly aligned. */
} heap_info;

/* Thread specific data */

static tsd_key_t arena_key;
static mutex_t list_lock;

/* number of arenas:  arena_count (not including the main_arena) */
static size_t arena_count;

/* 
 * a linked list of arena's of length arena_count ordered from smallest arena_index to
 * largest arena index. new arenas are added on the end. the last_arena points to the last
 * arena added. Note Bene: the main_arena has arena_index 1; thus the first arena in this
 * list has arena_index 2.
 */
static struct malloc_state *arena_list;
static struct malloc_state *last_arena;


static inline void _arena_is_sane(mchunkptr p, const char* file, int lineno){
  size_t count;
  
  count = __atomic_load_n(&arena_count, __ATOMIC_SEQ_CST);

  if(p->arena_index > count + 1){
    fprintf(stderr,  "arena_is_sane: %p->arena_index = %zu @ %s line %d\n", chunk2mem(p), p->arena_index, file, lineno);
  }
  assert(p->arena_index <= count + 1);
}


#if THREAD_STATS
static int stat_n_heaps;
#define THREAD_STAT(x) x
#else
#define THREAD_STAT(x) do ; while(0)
#endif

/* Mapped memory in non-main arenas (reliable only for NO_THREADS). */
static unsigned long arena_mem;

/* Already initialized? */
int __malloc_initialized = -1;

/**************************************************************************/

static bool do_check_top(mstate av, const char* file, int lineno);

static bool do_check_metadata_chunk(mstate av, mchunkptr c, chunkinfoptr ci, const char* file, int lineno);
  


#if USE_ARENAS

static mstate arena_get2(mstate a_tsd, size_t size);

/* arena_get() acquires an arena and locks the corresponding mutex.
   First, try the one last locked successfully by this thread.  (This
   is the common case and handled with a macro for speed.)  Then, loop
   once over the circularly linked list of arenas.  If no arena is
   readily available, create a new one.  In this latter case, `size'
   is just a hint as to how much memory will be required immediately
   in the new arena. */

#define arena_get(ptr, size) do { \
  Void_t *vptr = NULL; \
  ptr = (mstate)tsd_getspecific(arena_key, vptr); \
  if(ptr && !mutex_trylock(&ptr->mutex)) { \
    THREAD_STAT(++(ptr->stat_lock_direct)); \
  } else \
    ptr = arena_get2(ptr, (size)); \
} while(0)


static inline void sri_arena_get(mstate *aptr, INTERNAL_SIZE_T size){
  Void_t *vptr = NULL;
  mstate ptr;
  assert(aptr != NULL);

  ptr = (mstate)tsd_getspecific(arena_key, vptr);
  if(ptr && !mutex_trylock(&ptr->mutex)) {
    THREAD_STAT(++(ptr->stat_lock_direct));
  } else {
    ptr = arena_get2(ptr, size);
  }

  *aptr = ptr;
}

/* find the heap and corresponding arena for a given ptr */

#define heap_for_ptr(ptr) \
  ((heap_info *)((unsigned long)(ptr) & ~(HEAP_MAX_SIZE-1)))

static inline heap_info* sri_heap_for_ptr(void *ptr){
  return (heap_info *)((unsigned long)ptr & ~(HEAP_MAX_SIZE-1));
}

#define arena_for_chunk(ptr) \
 (chunk_non_main_arena(ptr) ? heap_for_ptr(ptr)->ar_ptr : &main_arena)

static inline mstate _arena_for_chunk(mchunkptr ptr){
  INTERNAL_SIZE_T index;
  mstate arena;
  size_t count;
  
  assert(ptr != NULL);

  index = arena_index(ptr);

  if(index < NON_MAIN_ARENA_INDEX){
    return &main_arena;
  }

  count = __atomic_load_n(&arena_count, __ATOMIC_SEQ_CST);
  
  index--;

  assert(index <= count);
  
  arena = main_arena.subsequent_arena;

  while(index > 1){
    arena = arena->subsequent_arena;
    index--;
  }
  
  assert(heap_for_ptr(ptr)->ar_ptr == arena);

  return arena;
}

#else /* !USE_ARENAS */

/* There is only one arena, main_arena. */

#if THREAD_STATS

#define arena_get(ar_ptr, sz) do { \
  ar_ptr = &main_arena; \
  if(!mutex_trylock(&ar_ptr->mutex)) \
    ++(ar_ptr->stat_lock_direct); \
  else { \
    (void)mutex_lock(&ar_ptr->mutex); \
    ++(ar_ptr->stat_lock_wait); \
  } \
} while(0)

static inline void sri_arena_get(mstate *aptr, INTERNAL_SIZE_T size){
  mstate ptr;
  assert(aptr != NULL);

  ptr = &main_arena;		     
  if(!mutex_trylock(&ptr->mutex)) 
    ++(ptr->stat_lock_direct); 
  else {			      
    (void)mutex_lock(&ptr->mutex); 
    ++(ptr->stat_lock_wait); 
  } 

  *aptr = ptr;
}


#else

#define arena_get(ar_ptr, sz) do { \
  ar_ptr = &main_arena; \
  (void)mutex_lock(&ar_ptr->mutex); \
} while(0)

static inline void sri_arena_get(mstate *aptr, INTERNAL_SIZE_T size){
  mstate ptr;
  assert(aptr != NULL);

  ptr = &main_arena;
  (void)mutex_lock(&ptr->mutex); 
  
  *aptr = ptr;
}

#endif

#define arena_for_chunk(ptr) (&main_arena)

static inline mstate sri_arena_for_chunk(void* ptr){
  return &main_arena;
}

#endif /* USE_ARENAS */

/**************************************************************************/

#ifndef NO_THREADS

/* atfork support.  */

static __malloc_ptr_t (*save_malloc_hook) __MALLOC_P ((size_t __size,
						       __const __malloc_ptr_t));
# if !defined _LIBC || !defined USE_TLS || (defined SHARED && !USE___THREAD)
static __malloc_ptr_t (*save_memalign_hook) __MALLOC_P ((size_t __align,
							 size_t __size,
						       __const __malloc_ptr_t));
# endif
static void           (*save_free_hook) __MALLOC_P ((__malloc_ptr_t __ptr,
						     __const __malloc_ptr_t));
static Void_t*        save_arena;

/* Magic value for the thread-specific arena pointer when
   malloc_atfork() is in use.  */

#define ATFORK_ARENA_PTR ((Void_t*)-1)

/* The following hooks are used while the `atfork' handling mechanism
   is active. */

static Void_t*
malloc_atfork(size_t sz, const Void_t *caller)
{
  Void_t *vptr = NULL;
  Void_t *victim;
  chunkinfoptr _md_victim;

  tsd_getspecific(arena_key, vptr);
  if(vptr == ATFORK_ARENA_PTR) {
    /* We are the only thread that may allocate at all.  */
    if(save_malloc_hook != malloc_check) {
      _md_victim = _int_malloc(&main_arena, sz);
      return chunkinfo2mem(_md_victim);

    } else {
      if(top_check()<0)
        return 0;
      _md_victim = _int_malloc(&main_arena, sz+1);
      victim = chunkinfo2mem(_md_victim);
      return mem2mem_check(victim, sz);
    }
  } else {
    /* Suspend the thread until the `atfork' handlers have completed.
       By that time, the hooks will have been reset as well, so that
       mALLOc() can be used again. */
    (void)mutex_lock(&list_lock);
    (void)mutex_unlock(&list_lock);
    return public_mALLOc(sz);
  }
}

static void
free_atfork(Void_t* mem, const Void_t *caller)
{
  Void_t *vptr = NULL;
  mstate ar_ptr;
  mchunkptr p;                          /* chunk corresponding to mem */
  chunkinfoptr _md_p;                   /* metadata of chunk  */

  if (mem == 0)                         /* free(0) has no effect */
    return;

  p = mem2chunk(mem);                   /* do not bother to replicate free_check here */

#if HAVE_MMAP
  if (chunk_is_mmapped(p))              /* release mmapped memory. */
  {
    _md_p = hashtable_lookup(&main_arena, p);  //iam: we should have the lock here, right c/f ptmalloc_lock_all.
    munmap_chunk(_md_p);
    return;
  }
#endif

  ar_ptr = arena_for_chunk(p);

  _md_p = hashtable_lookup(ar_ptr, p);

  if(_md_p == NULL){
    missing_metadata(ar_ptr, p);
  } 

  tsd_getspecific(arena_key, vptr);
  if(vptr != ATFORK_ARENA_PTR)
    (void)mutex_lock(&ar_ptr->mutex);

  do_check_top(ar_ptr, __FILE__, __LINE__);

  _int_free(ar_ptr, _md_p);
  if(vptr != ATFORK_ARENA_PTR)
    (void)mutex_unlock(&ar_ptr->mutex);
}

/* The following two functions are registered via thread_atfork() to
   make sure that the mutexes remain in a consistent state in the
   fork()ed version of a thread.  Also adapt the malloc and free hooks
   temporarily, because the `atfork' handler mechanism may use
   malloc/free internally (e.g. in LinuxThreads). */

static void
ptmalloc_lock_all __MALLOC_P((void))
{
  mstate ar_ptr;

  if(__malloc_initialized < 1)
    return;
  (void)mutex_lock(&list_lock);
  for(ar_ptr = &main_arena;;) {
    (void)mutex_lock(&ar_ptr->mutex);
    ar_ptr = ar_ptr->next;
    if(ar_ptr == &main_arena) break;
  }
  save_malloc_hook = __malloc_hook;
  save_free_hook = __free_hook;
  __malloc_hook = malloc_atfork;
  __free_hook = free_atfork;
  /* Only the current thread may perform malloc/free calls now. */
  tsd_getspecific(arena_key, save_arena);
  tsd_setspecific(arena_key, ATFORK_ARENA_PTR);
}

static void
ptmalloc_unlock_all __MALLOC_P((void))
{
  mstate ar_ptr;

  if(__malloc_initialized < 1)
    return;
  tsd_setspecific(arena_key, save_arena);
  __malloc_hook = save_malloc_hook;
  __free_hook = save_free_hook;
  for(ar_ptr = &main_arena;;) {
    (void)mutex_unlock(&ar_ptr->mutex);
    ar_ptr = ar_ptr->next;
    if(ar_ptr == &main_arena) break;
  }
  (void)mutex_unlock(&list_lock);
}

#ifdef __linux__

/* In LinuxThreads, unlocking a mutex in the child process after a
   fork() is currently unsafe, whereas re-initializing it is safe and
   does not leak resources.  Therefore, a special atfork handler is
   installed for the child. */

static void
ptmalloc_unlock_all2 __MALLOC_P((void))
{
  mstate ar_ptr;

  if(__malloc_initialized < 1)
    return;
#if defined _LIBC || defined MALLOC_HOOKS
  tsd_setspecific(arena_key, save_arena);
  __malloc_hook = save_malloc_hook;
  __free_hook = save_free_hook;
#endif
  for(ar_ptr = &main_arena;;) {
    (void)mutex_init(&ar_ptr->mutex);
    ar_ptr = ar_ptr->next;
    if(ar_ptr == &main_arena) break;
  }
  (void)mutex_init(&list_lock);
}

#else

#define ptmalloc_unlock_all2 ptmalloc_unlock_all

#endif

#endif /* !defined NO_THREADS */

/* Initialization routine. */
#ifdef _LIBC
#include <string.h>
extern char **_environ;

static char *
internal_function
next_env_entry (char ***position)
{
  char **current = *position;
  char *result = NULL;

  while (*current != NULL)
    {
      if (__builtin_expect ((*current)[0] == 'M', 0)
	  && (*current)[1] == 'A'
	  && (*current)[2] == 'L'
	  && (*current)[3] == 'L'
	  && (*current)[4] == 'O'
	  && (*current)[5] == 'C'
	  && (*current)[6] == '_')
	{
	  result = &(*current)[7];

	  /* Save current position for next visit.  */
	  *position = ++current;

	  break;
	}

      ++current;
    }

  return result;
}
#endif /* _LIBC */

/* Set up basic state so that _int_malloc et al can work.  */
static void
ptmalloc_init_minimal __MALLOC_P((void))
{
#if DEFAULT_TOP_PAD != 0
  mp_.top_pad        = DEFAULT_TOP_PAD;
#endif
  mp_.n_mmaps_max    = DEFAULT_MMAP_MAX;
  mp_.mmap_threshold = DEFAULT_MMAP_THRESHOLD;
  mp_.trim_threshold = DEFAULT_TRIM_THRESHOLD;
  mp_.pagesize       = malloc_getpagesize;
}


#if !(USE_STARTER & 2)
static
#endif
void
ptmalloc_init __MALLOC_P((void))
{
#if __STD_C
  const char* s;
#else
  char* s;
#endif
  int secure = 0;

  if(__malloc_initialized >= 0) return;
  __malloc_initialized = 0;

  if (mp_.pagesize == 0)
    ptmalloc_init_minimal();

#ifndef NO_THREADS
# if USE_STARTER & 1
  /* With some threads implementations, creating thread-specific data
     or initializing a mutex may call malloc() itself.  Provide a
     simple starter version (realloc() won't work). */
  save_malloc_hook = __malloc_hook;
  save_memalign_hook = __memalign_hook;
  save_free_hook = __free_hook;
  __malloc_hook = malloc_starter;
  __memalign_hook = memalign_starter;
  __free_hook = free_starter;
#  ifdef _LIBC
  /* Initialize the pthreads interface. */
  if (__pthread_initialize != NULL)
    __pthread_initialize();
#  endif /* !defined _LIBC */
# endif /* USE_STARTER & 1 */
#endif /* !defined NO_THREADS */
  mutex_init(&main_arena.mutex);
  main_arena.next = &main_arena;

  mutex_init(&list_lock);
  tsd_key_create(&arena_key, NULL);
  tsd_setspecific(arena_key, (Void_t *)&main_arena);
  thread_atfork(ptmalloc_lock_all, ptmalloc_unlock_all, ptmalloc_unlock_all2);
#ifndef NO_THREADS
# if USE_STARTER & 1
  __malloc_hook = save_malloc_hook;
  __memalign_hook = save_memalign_hook;
  __free_hook = save_free_hook;
# endif
# if USE_STARTER & 2
  __malloc_hook = 0;
  __memalign_hook = 0;
  __free_hook = 0;
# endif
#endif
#ifdef _LIBC
  secure = __libc_enable_secure;
  s = NULL;
  if (__builtin_expect (_environ != NULL, 1))
    {
      char **runp = _environ;
      char *envline;

      while (__builtin_expect ((envline = next_env_entry (&runp)) != NULL,
			       0))
	{
	  size_t len = strcspn (envline, "=");

	  if (envline[len] != '=')
	    /* This is a "MALLOC_" variable at the end of the string
	       without a '=' character.  Ignore it since otherwise we
	       will access invalid memory below.  */
	    continue;

	  switch (len)
	    {
	    case 6:
	      if (memcmp (envline, "CHECK_", 6) == 0)
		s = &envline[7];
	      break;
	    case 8:
	      if (! secure && memcmp (envline, "TOP_PAD_", 8) == 0)
		mALLOPt(M_TOP_PAD, atoi(&envline[9]));
	      break;
	    case 9:
	      if (! secure && memcmp (envline, "MMAP_MAX_", 9) == 0)
		mALLOPt(M_MMAP_MAX, atoi(&envline[10]));
	      break;
	    case 15:
	      if (! secure)
		{
		  if (memcmp (envline, "TRIM_THRESHOLD_", 15) == 0)
		    mALLOPt(M_TRIM_THRESHOLD, atoi(&envline[16]));
		  else if (memcmp (envline, "MMAP_THRESHOLD_", 15) == 0)
		    mALLOPt(M_MMAP_THRESHOLD, atoi(&envline[16]));
		}
	      break;
	    default:
	      break;
	    }
	}
    }
#else
  if (! secure)
    {
      if((s = getenv("MALLOC_TRIM_THRESHOLD_")))
	mALLOPt(M_TRIM_THRESHOLD, atoi(s));
      if((s = getenv("MALLOC_TOP_PAD_")))
	mALLOPt(M_TOP_PAD, atoi(s));
      if((s = getenv("MALLOC_MMAP_THRESHOLD_")))
	mALLOPt(M_MMAP_THRESHOLD, atoi(s));
      if((s = getenv("MALLOC_MMAP_MAX_")))
	mALLOPt(M_MMAP_MAX, atoi(s));
    }
  s = getenv("MALLOC_CHECK_");
#endif
  if(s) {
    if(s[0]) mALLOPt(M_CHECK_ACTION, (int)(s[0] - '0'));
    __malloc_check_init();
  }
  if(__malloc_initialize_hook != NULL)
    (*__malloc_initialize_hook)();
  __malloc_initialized = 1;
}

/* There are platforms (e.g. Hurd) with a link-time hook mechanism. */
#ifdef thread_atfork_static
thread_atfork_static(ptmalloc_lock_all, ptmalloc_unlock_all, \
                     ptmalloc_unlock_all2)
#endif



/* Managing heaps and arenas (for concurrent threads) */

#if USE_ARENAS

#if MALLOC_DEBUG > 1

/* Print the complete contents of a single heap to stderr. */

static void
#if __STD_C
dump_heap(heap_info *heap)
#else
dump_heap(heap) heap_info *heap;
#endif
{
  char *ptr;
  mchunkptr p;                                                        
  mchunkptr top;

  top = chunkinfo2chunk((heap->ar_ptr)->_md_top);

  fprintf(stderr, "Heap %p, size %10lx:\n", heap, (long)heap->size);
  
  ptr = (heap->ar_ptr != (mstate)(heap+1)) ?
    (char*)(heap + 1) : (char*)(heap + 1) + sizeof(struct malloc_state);

  p = (mchunkptr)(((unsigned long)ptr + MALLOC_ALIGN_MASK) &
                  ~MALLOC_ALIGN_MASK);
  for(;;) {
    fprintf(stderr, "chunk %p size %10lx", p, (long)p->size);
    if(p == top) {
      fprintf(stderr, " (top)\n");
      break;
    } else if(p->size == (0|PREV_INUSE)) {
      fprintf(stderr, " (fence)\n");
      break;
    }
    fprintf(stderr, "\n");
    p = next_chunk(p);
  }
}

#endif /* MALLOC_DEBUG > 1 */

/* Create a new heap.  size is automatically rounded up to a multiple
   of the page size. */

static heap_info *
internal_function
#if __STD_C
new_heap(size_t size, size_t top_pad)
#else
new_heap(size, top_pad) size_t size, top_pad;
#endif
{
  size_t page_mask = malloc_getpagesize - 1;
  char *p1, *p2;
  unsigned long ul;
  heap_info *h;

  if(size+top_pad < HEAP_MIN_SIZE)
    size = HEAP_MIN_SIZE;
  else if(size+top_pad <= HEAP_MAX_SIZE)
    size += top_pad;
  else if(size > HEAP_MAX_SIZE)
    return 0;
  else
    size = HEAP_MAX_SIZE;
  size = (size + page_mask) & ~page_mask;

  /* A memory region aligned to a multiple of HEAP_MAX_SIZE is needed.
     No swap space needs to be reserved for the following large
     mapping (on Linux, this is the case for all non-writable mappings
     anyway). */
  p1 = (char *)MMAP(0, HEAP_MAX_SIZE<<1, PROT_NONE, MAP_PRIVATE|MAP_NORESERVE);
  if(p1 != MAP_FAILED) {
    p2 = (char *)(((unsigned long)p1 + (HEAP_MAX_SIZE-1)) & ~(HEAP_MAX_SIZE-1));
    ul = p2 - p1;
    munmap(p1, ul);
    munmap(p2 + HEAP_MAX_SIZE, HEAP_MAX_SIZE - ul);
  } else {
    /* Try to take the chance that an allocation of only HEAP_MAX_SIZE
       is already aligned. */
    p2 = (char *)MMAP(0, HEAP_MAX_SIZE, PROT_NONE, MAP_PRIVATE|MAP_NORESERVE);
    if(p2 == MAP_FAILED)
      return 0;
    if((unsigned long)p2 & (HEAP_MAX_SIZE-1)) {
      munmap(p2, HEAP_MAX_SIZE);
      return 0;
    }
  }
  if(mprotect(p2, size, PROT_READ|PROT_WRITE) != 0) {
    munmap(p2, HEAP_MAX_SIZE);
    return 0;
  }
  h = (heap_info *)p2;
  h->size = size;
  THREAD_STAT(stat_n_heaps++);
  return h;
}

/* Grow or shrink a heap.  size is automatically rounded up to a
   multiple of the page size if it is positive. */

static int
#if __STD_C
grow_heap(heap_info *h, long diff)
#else
grow_heap(h, diff) heap_info *h; long diff;
#endif
{
  size_t page_mask = malloc_getpagesize - 1;
  long new_size;

  if(diff >= 0) {
    diff = (diff + page_mask) & ~page_mask;
    new_size = (long)h->size + diff;
    if(new_size > HEAP_MAX_SIZE)
      return -1;
    if(mprotect((char *)h + h->size, diff, PROT_READ|PROT_WRITE) != 0)
      return -2;
  } else {
    new_size = (long)h->size + diff;
    if(new_size < (long)sizeof(*h))
      return -1;
    /* Try to re-map the extra heap space freshly to save memory, and
       make it inaccessible. */
    if((char *)MMAP((char *)h + new_size, -diff, PROT_NONE,
                    MAP_PRIVATE|MAP_FIXED) == (char *) MAP_FAILED)
      return -2;
    /* fprintf(stderr, "shrink %p %08lx\n", h, new_size);  iam: hmmm looks like we are still debugging here? */
  }
  h->size = new_size;
  return 0;
}
static void
internal_function
#if __STD_C
heap_sysmalloc(INTERNAL_SIZE_T nb, mstate av)
#else
heap_sysmalloc(nb, av) INTERNAL_SIZE_T nb; mstate av;
#endif
{
    heap_info *old_heap, *heap;
    size_t old_heap_size;

    mchunkptr       top;            /* for updating av->_md_top */
    
    mchunkptr       old_top;        /* incoming value of av->_md_top's chunk */
    chunkinfoptr    _md_old_top;    /* incoming value of av->_md_top  */
    INTERNAL_SIZE_T old_size;       /* its size */
   
    mchunkptr fencepost;            /* fenceposts */
    chunkinfoptr _md_fencepost;     /* metadata of the fenceposts */

    /* the min-ish sized chunk */
    mchunkptr minpost;
    chunkinfoptr _md_minpost;

    /* Record incoming configuration of top */

    old_top  = chunkinfo2chunk(av->_md_top);
    _md_old_top  = av->_md_top;

    old_size = chunksize(_md_old_top);
    
    /* First try to extend the current heap. */
    old_heap = heap_for_ptr(old_top);
    old_heap_size = old_heap->size;
    if (grow_heap(old_heap, MINSIZE + nb - old_size) == 0) {
      av->system_mem += old_heap->size - old_heap_size;
      arena_mem += old_heap->size - old_heap_size;
      set_head(av, _md_old_top, old_top, (((char *)old_heap + old_heap->size) - (char *)old_top)  | PREV_INUSE);
    }
    else if ((heap = new_heap(nb + (MINSIZE + sizeof(*heap)), mp_.top_pad))) {
      /* Use a newly allocated heap.  */
      heap->ar_ptr = av;
      heap->prev = old_heap;                                                    
      av->system_mem += heap->size;
      arena_mem += heap->size;

      /* Set up the new top. */                                                
      top = chunk_at_offset(heap, sizeof(*heap));
      av->_md_top = create_metadata(av, top);
      set_head(av, av->_md_top, top, (heap->size - sizeof(*heap)) | PREV_INUSE);


      /* Setup fencepost and free the old top chunk. */
      /* The fencepost takes at least MINSIZE bytes, because it might
         become the top chunk again later.  Note that a footer is set
         up, too, although the chunk is marked in use. */
      old_size -= MINSIZE;

      fencepost = chunk_at_offset(old_top, old_size + 2*SIZE_SZ);
      _md_fencepost = create_metadata(av, fencepost);
      set_head(av, _md_fencepost, fencepost, 0|PREV_INUSE);

      if (old_size >= MINSIZE) {

        minpost = chunk_at_offset(old_top, old_size);
	_md_minpost = create_metadata(av, minpost);

        set_head(av, _md_minpost, minpost, (2*SIZE_SZ)|PREV_INUSE);
        set_foot(av, _md_minpost, minpost, (2*SIZE_SZ));
        
        set_head(av, _md_old_top, old_top, old_size|PREV_INUSE);

        _int_free(av, _md_old_top);

      } else {

        set_head(av, _md_old_top, old_top, (old_size + 2*SIZE_SZ)|PREV_INUSE);
        set_foot(av, _md_old_top, old_top, (old_size + 2*SIZE_SZ));

      }
    }
}

/* Delete a heap. */

#define delete_heap(heap) munmap((char*)(heap), HEAP_MAX_SIZE)

static int
internal_function
#if __STD_C
heap_trim(heap_info *heap, size_t pad)
#else
heap_trim(heap, pad) heap_info *heap; size_t pad;
#endif
{
  mstate ar_ptr = heap->ar_ptr;
  unsigned long pagesz = mp_.pagesize;
  mchunkptr top_chunk = chunkinfo2chunk(ar_ptr->_md_top);

  mchunkptr p;
  chunkinfoptr _md_p;
  
  chunkinfoptr bck, fwd;
  heap_info *prev_heap;
  long new_size, top_size, extra;

  int iterations = 0;

  /* a full heap ends in a fencepost; and a min-ish sized chunk */

  /* the fencepost */
  mchunkptr fencepost;
  chunkinfoptr _md_fencepost;

  /* the min-ish sized chunk */
  mchunkptr minpost;
  chunkinfoptr _md_minpost;


  do_check_top(ar_ptr, __FILE__, __LINE__);

  /* Can this heap go away completely? */

  /*
   * iam: the mstate part of the header only occurs in the first heap
   * of this arena. all subsequent heaps in the arena start out
   * with their top being the next pointer after the heap_info
   * header.
   *
   * so this condition is asking: is this non-first heap of
   * this arena empty.
  */
  while(top_chunk == chunk_at_offset(heap, sizeof(*heap))) {    
    iterations++;
    /* iam: note that we know prev_heap is not null, because we are in a non-first heap of this arena. */
    prev_heap = heap->prev;

    /* iam: we are going to delete this heap and consolidate the tail of the previous heap */

    /* get the fencepost at the end */
    fencepost = chunk_at_offset(prev_heap, prev_heap->size - (MINSIZE-2*SIZE_SZ));
    _md_fencepost = hashtable_lookup(ar_ptr, fencepost);
    
    if(_md_fencepost == NULL){
      missing_metadata(ar_ptr, fencepost);
      return 0;
    }
    
    assert(_md_fencepost->size == (0|PREV_INUSE)); /* must be fencepost */

    minpost = prev_chunk(_md_fencepost, fencepost);
    
    _md_minpost = hashtable_lookup(ar_ptr, minpost);
    
    if(_md_minpost == NULL){
      missing_metadata(ar_ptr, minpost);
      return 0;
    } 

    new_size = chunksize(_md_minpost) + (MINSIZE-2*SIZE_SZ);  /* iam: pulling out the fencepost! */
    
    assert(new_size>0 && new_size<(long)(2*MINSIZE));  /* must be minpost */ 

    if(!prev_inuse(_md_minpost, minpost)){
      new_size += get_prev_size(_md_minpost, minpost);   
    }
    assert(new_size>0 && new_size<HEAP_MAX_SIZE);    

    if(new_size + (HEAP_MAX_SIZE - prev_heap->size) < pad + MINSIZE + pagesz){  /* iam: ? */
      break; 
    }

    hashtable_remove(ar_ptr, fencepost, 8);      /* iam: out comes the fencepost  */

    ar_ptr->system_mem -= heap->size;
    arena_mem -= heap->size;

    /* iam: we should remove this heap's top_chunk from our hashtable  */
    hashtable_remove(ar_ptr, top_chunk, 9);
    delete_heap(heap);
    heap = prev_heap;
    
    if(!prev_inuse(_md_minpost, minpost)) {
      /* consolidate backward  */
      p = prev_chunk(_md_minpost, minpost);
      hashtable_remove(ar_ptr, minpost, 10);      /* iam: out comes the minpost  */
      _md_p = hashtable_lookup(ar_ptr, p);

      if(_md_p == NULL){
	missing_metadata(ar_ptr, p);
	return 0;
      } 
      
      ps_unlink(_md_p, &bck, &fwd);
      
    } else {
      p = minpost;
      _md_p = _md_minpost;
    }

    
    assert(((unsigned long)((char*)p + new_size) & (pagesz-1)) == 0);

    assert( ((char*)p + new_size) == ((char*)heap + heap->size) );
       
    /* iam: we need to get metadata of so we can update it and store it */
    top_chunk = p;
    ar_ptr->_md_top = _md_p;
    set_head(ar_ptr, _md_p, top_chunk, new_size | PREV_INUSE);  

    do_check_metadata_chunk(ar_ptr, p, _md_p, __FILE__, __LINE__);
    
    /* iam: wonder why this was commented out? check_chunk(ar_ptr, top_chunk); */

  } /* while */

  
  top_size = chunksize(ar_ptr->_md_top);
  extra = ((top_size - pad - MINSIZE + (pagesz-1))/pagesz - 1) * pagesz;
  if(extra < (long)pagesz)
    return 0;
  /* Try to shrink. */
  if(grow_heap(heap, -extra) != 0)
    return 0;
  ar_ptr->system_mem -= extra;
  arena_mem -= extra;
  /* Success. Adjust top accordingly. */
  set_head(ar_ptr, ar_ptr->_md_top, top_chunk, (top_size - extra) | PREV_INUSE);   

  /*iam: wonder why this was commented out? check_chunk(ar_ptr, top_chunk);*/
  return iterations++;
}

static int stepper;

#define max(X,Y) (X < Y ? Y : X)

static mstate
internal_function
#if __STD_C
arena_get2(mstate a_tsd, size_t size)
#else
arena_get2(a_tsd, size) mstate a_tsd; size_t size;
#endif
{
  mstate a;
  int err;
  size_t count;
  
  Void_t *vptr = NULL;
  
  stepper = max(stepper, 1);
  
  if(!a_tsd) {

    a = a_tsd = &main_arena;
  }
  else {
    a = a_tsd->next;

    stepper =  max(stepper, 2);
    

    if(!a) {
      /* This can only happen while initializing the new arena. */
      (void)mutex_lock(&main_arena.mutex);
      THREAD_STAT(++(main_arena.stat_lock_wait));
      return &main_arena;
    }
  }

  
  /* Check the global, circularly linked list for available arenas. */
 repeat:

  stepper =  max(stepper, 3);

  
  do {
    if(!mutex_trylock(&a->mutex)) {
      THREAD_STAT(++(a->stat_lock_loop));
      tsd_setspecific(arena_key, (Void_t *)a);
      return a;
    }
    a = a->next;
  } while(a != a_tsd);
  

  /* If not even the list_lock can be obtained, try again.  This can
     happen during `atfork', or for example on systems where thread
     creation makes it temporarily impossible to obtain _any_
     locks. */
  if(mutex_trylock(&list_lock)) {
    a = a_tsd;
    goto repeat;
  }
  (void)mutex_unlock(&list_lock);

  stepper =  max(stepper, 4);

  /* Nothing immediately available, so generate a new arena.  */
  a = _int_new_arena(size);
  if(!a){
    return 0;
  }

  /* make sure "a" has a sane arena_index */
  /* increment (non main arena) arena_count */
  count = __atomic_add_fetch(&arena_count, 1, __ATOMIC_SEQ_CST);
  /* index is one more, since main_arena has index 1 */
  a->arena_index = count + 1;    

  stepper =  max(stepper, 5);

  mutex_init(&a->mutex);
  
  stepper =  max(stepper, 6);

  assert(&a->mutex != NULL);
  err = mutex_lock(&a->mutex); /* remember result */

  stepper =  max(stepper, 7);
   
  /* Add the new arena to the global list.  */
  (void)mutex_lock(&list_lock);
  a->next = main_arena.next;
  atomic_write_barrier ();
  main_arena.next = a;
  if(arena_list == NULL){
    assert(last_arena == NULL);
    arena_list = a;
  } else {
    if(last_arena != NULL){
      last_arena->subsequent_arena = a;
    }
  }
  last_arena = a;
  (void)mutex_unlock(&list_lock);

  stepper =  max(stepper, 8);
  if(err) /* locking failed; keep arena for further attempts later */
    return 0;

  stepper =  max(stepper, 9);
  
  THREAD_STAT(++(a->stat_lock_loop));


  tsd_setspecific(arena_key, (Void_t *)a);
  
  tsd_getspecific(arena_key, vptr);
  assert(a == vptr);

  return a;
}

/* Create a new arena with initial size "size".  */

mstate
_int_new_arena(size_t size)
{
  mstate a;
  heap_info *h;
  char *ptr;
  unsigned long misalign;
  mchunkptr top;
  
  h = new_heap(size + (sizeof(*h) + sizeof(*a) + MALLOC_ALIGNMENT),
	       mp_.top_pad);
  if(!h) {
    /* Maybe size is too large to fit in a single heap.  So, just try
       to create a minimally-sized arena and let _int_malloc() attempt
       to deal with the large request via mmap_chunk().  */
    h = new_heap(sizeof(*h) + sizeof(*a) + MALLOC_ALIGNMENT, mp_.top_pad);
    if(!h)
      return 0;
  }
  a = h->ar_ptr = (mstate)(h+1);
  malloc_init_state(a, false);
  /*a->next = NULL;*/
  a->system_mem = a->max_system_mem = h->size;
  arena_mem += h->size;
#ifdef NO_THREADS
  if((unsigned long)(mp_.mmapped_mem + arena_mem + main_arena.system_mem) >
     mp_.max_total_mem)
    mp_.max_total_mem = mp_.mmapped_mem + arena_mem + main_arena.system_mem;
#endif

  /* Set up the top chunk, with proper alignment. */
  ptr = (char *)(a + 1);
  misalign = (unsigned long)chunk2mem(ptr) & MALLOC_ALIGN_MASK;
  if (misalign > 0)
    ptr += MALLOC_ALIGNMENT - misalign;
  top = (mchunkptr)ptr;
  a->_md_top = create_metadata(a, top);
  set_head(a, a->_md_top, top, (((char*)h + h->size) - ptr) | PREV_INUSE);


  do_check_top(a, __FILE__, __LINE__);

  return a;
}

/* Obtain the arena number n.  Needed in malloc_stats.  */

mstate
_int_get_arena (int n)
{
  mstate a = &main_arena;

  while (n-- != 0) {
    a = a->next;
    if (a == &main_arena)
      return 0;
  }
  return a;
}

#else 

/* iam: dummy for the !USE_ARENAS case */
mstate
_int_get_arena (int n)
{
  assert(n == 0);
  return &main_arena;
}


#endif /* USE_ARENAS */

/*
 * Local variables:
 * c-basic-offset: 2
 * End:
 */
