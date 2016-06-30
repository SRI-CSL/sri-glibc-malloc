/* Malloc implementation for multiple threads without lock contention.
   Copyright (C) 2001-2015 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Wolfram Gloger <wg@malloc.de>, 2001.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <http://www.gnu.org/licenses/>.  */

#include <stdbool.h>

#include "lookup.h"

/***************************************************************************/

/* A heap is a single contiguous memory region holding (coalesceable)
   malloc_chunks.  It is allocated with mmap() and always starts at an
   address aligned to HEAP_MAX_SIZE.  */

typedef struct _heap_info
{
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t mprotect_size; /* Size in bytes that has been mprotected
                           PROT_READ|PROT_WRITE.  */
  /* Make sure the following data is properly aligned, particularly
     that sizeof (heap_info) + 2 * SIZE_SZ is a multiple of
     MALLOC_ALIGNMENT. */
  char pad[-6 * SIZE_SZ & MALLOC_ALIGN_MASK];
} heap_info;

/* Get a compile-time error if the heap_info padding is not correct
   to make alignment work as expected in sYSMALLOc.  */
extern int sanity_check_heap_info_alignment[(sizeof (heap_info)
                                             + 2 * SIZE_SZ) % MALLOC_ALIGNMENT
                                            ? -1 : 1];

/* Thread specific data.  */

static __thread mstate thread_arena attribute_tls_model_ie;

/* Arena free list.  list_lock protects the free_list variable below,
   and the next_free and attached_threads members of the mstate
   objects.  No other (malloc) locks must be taken while list_lock is
   active, otherwise deadlocks may occur.  */

static mutex_t list_lock = _LIBC_LOCK_INITIALIZER;
static size_t narenas = 1;
static mstate free_list;

/* Mapped memory in non-main arenas (reliable only for NO_THREADS). */
static unsigned long arena_mem;

/* Already initialized? */
int __malloc_initialized = -1;


/* SRI Additions:
 *
 * We have modified the main_arena.next list, so that new arenas are added on the end.
 * Thus it is a cyclic list of "length" of length arena_count ordered from 
 * smallest arena_index to largest arena index.
 * Note Bene: the main_arena has arena_index 1; thus the first non-main arena in this
 * list has arena_index 2.
 */

/* the last arena in the main_arena.next list */
static struct malloc_state *last_arena = &main_arena;

/* number of arenas: arena_count. The narenas counter is not protected
 * by the list_lock so we can't use that.
 */
static size_t arena_count = 1;


/**************************************************************************/


/* arena_get() acquires an arena and locks the corresponding mutex.
   First, try the one last locked successfully by this thread.  (This
   is the common case and handled with a macro for speed.)  Then, loop
   once over the circularly linked list of arenas.  If no arena is
   readily available, create a new one.  In this latter case, `size'
   is just a hint as to how much memory will be required immediately
   in the new arena. */

#define arena_get(ptr, size, site) do {					\
      ptr = thread_arena;						      \
      arena_lock (ptr, size, site);						\
  } while (0)

#define arena_lock(ptr, size, site) do {					\
      if (ptr && !arena_is_corrupt (ptr))				      \
        LOCK_ARENA(ptr, site);						\
      else								      \
        ptr = arena_get2 ((size), NULL);				      \
  } while (0)

/* find the heap and corresponding arena for a given ptr */

#define heap_for_ptr(ptr) \
  ((heap_info *)((unsigned long) (ptr) & ~(HEAP_MAX_SIZE - 1)))
#define arena_for_chunk(ptr) \
  (chunk_non_main_arena (ptr) ? heap_for_ptr (ptr)->ar_ptr : &main_arena)


/* SRI: check that the arena_index of a chunk make sense */
static bool _arena_is_sane(mchunkptr p, const char* file, int lineno){
  size_t count = __atomic_load_n(&arena_count, __ATOMIC_SEQ_CST);
  return p->arena_index <= count;
}

/* SRI:
 *
 * Returns the arena with the same index as ptr.  Though we try to
 * preserve the order in the cyclic linked list of arenas it is not
 * guaranteed. So we go through the list until we find it.
 *
 *
 */
static mstate arena_from_chunk(mchunkptr ptr){
  INTERNAL_SIZE_T index;
  mstate arena;
  size_t count;
  
  assert(ptr != NULL);

  index = get_arena_index(ptr);

  if(index < NON_MAIN_ARENA_INDEX){
    return &main_arena;
  }

  count = __atomic_load_n(&arena_count, __ATOMIC_SEQ_CST);
  

  assert(index  <= count + 1);

  arena = main_arena.next;
  
  while(count > 0 && arena->arena_index != index){
    arena = arena->next;
    count--;
  }

  assert(arena != NULL);
  assert(arena->arena_index == index);
  
  /* unsafe; but just a sanity check */
  assert((heap_for_ptr(ptr))->ar_ptr == arena);

  return arena;
}


/**************************************************************************/

#ifndef NO_THREADS

/* atfork support.  */

static void *(*save_malloc_hook)(size_t __size, const void *);
static void (*save_free_hook) (void *__ptr, const void *);
static void *save_arena;

# ifdef ATFORK_MEM
ATFORK_MEM;
# endif

/* Magic value for the thread-specific arena pointer when
   malloc_atfork() is in use.  */

# define ATFORK_ARENA_PTR ((void *) -1)

/* The following hooks are used while the `atfork' handling mechanism
   is active. */

static void *
malloc_atfork (size_t sz, const void *caller)
{
  chunkinfoptr _md_victim;
  void *mem;

  if (thread_arena == ATFORK_ARENA_PTR)
    {
      /* We are the only thread that may allocate at all.  */
      if (save_malloc_hook != malloc_check)
        {
	  _md_victim = _int_malloc (&main_arena, sz);
          return chunkinfo2mem(_md_victim);
        }
      else
        {
          if (top_check () < 0)
            return 0;

          _md_victim = _int_malloc (&main_arena, sz + 1);
	  mem = chunkinfo2mem(_md_victim);
          return mem2mem_check (_md_victim, chunkinfo2chunk(_md_victim), mem, sz);
        }
    }
  else
    {
      /* Suspend the thread until the `atfork' handlers have completed.
         By that time, the hooks will have been reset as well, so that
         mALLOc() can be used again. */
      (void) mutex_lock (&list_lock);
      (void) mutex_unlock (&list_lock);
      return __libc_malloc (sz);
    }
}

static void
free_atfork (void *mem, const void *caller)
{
  mstate ar_ptr;
  mchunkptr p;                 /* chunk corresponding to mem */
  chunkinfoptr _md_p;          /* metadata of chunk  */


  if (mem == 0)                /* free(0) has no effect */
    return;

  p = mem2chunk (mem);         /* do not bother to replicate free_check here */

  if (chunk_is_mmapped (p))    /* release mmapped memory. */
    {
      _md_p = lookup_chunk(&main_arena, p);  //SRI: do we have the lock here?
      munmap_chunk(_md_p);
      return;
    }

  ar_ptr = arena_for_chunk (p);
  _int_free (ar_ptr, NULL, p, thread_arena == ATFORK_ARENA_PTR);
}


/* Counter for number of times the list is locked by the same thread.  */
static unsigned int atfork_recursive_cntr;

/* The following two functions are registered via thread_atfork() to
   make sure that the mutexes remain in a consistent state in the
   fork()ed version of a thread.  Also adapt the malloc and free hooks
   temporarily, because the `atfork' handler mechanism may use
   malloc/free internally (e.g. in LinuxThreads). */

static void
ptmalloc_lock_all (void)
{
  mstate ar_ptr;
  int success;

  if (__malloc_initialized < 1)
    return;
  success = mutex_trylock (&list_lock);
  if (success)
    {
      if (thread_arena == ATFORK_ARENA_PTR)
        /* This is the same thread which already locks the global list.
           Just bump the counter.  */
        goto out;

      /* This thread has to wait its turn.  */
      (void) mutex_lock (&list_lock);
    }
  for (ar_ptr = &main_arena;; )
    {
      LOCK_ARENA(ar_ptr, ARENA_SITE);
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
  save_malloc_hook = __malloc_hook;
  save_free_hook = __free_hook;
  __malloc_hook = malloc_atfork;
  __free_hook = free_atfork;
  /* Only the current thread may perform malloc/free calls now.
     save_arena will be reattached to the current thread, in
     ptmalloc_lock_all, so save_arena->attached_threads is not
     updated.  */
  save_arena = thread_arena;
  thread_arena = ATFORK_ARENA_PTR;
out:
  ++atfork_recursive_cntr;
}

static void
ptmalloc_unlock_all (void)
{
  mstate ar_ptr;

  if (__malloc_initialized < 1)
    return;

  if (--atfork_recursive_cntr != 0)
    return;

  /* Replace ATFORK_ARENA_PTR with save_arena.
     save_arena->attached_threads was not changed in ptmalloc_lock_all
     and is still correct.  */
  thread_arena = save_arena;
  __malloc_hook = save_malloc_hook;
  __free_hook = save_free_hook;
  for (ar_ptr = &main_arena;; )
    {
      UNLOCK_ARENA(ar_ptr, ARENA_SITE);
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
  (void) mutex_unlock (&list_lock);
}

# ifdef __linux__

/* In NPTL, unlocking a mutex in the child process after a
   fork() is currently unsafe, whereas re-initializing it is safe and
   does not leak resources.  Therefore, a special atfork handler is
   installed for the child. */

static void
ptmalloc_unlock_all2 (void)
{
  mstate ar_ptr;

  if (__malloc_initialized < 1)
    return;

  thread_arena = save_arena;
  __malloc_hook = save_malloc_hook;
  __free_hook = save_free_hook;

  /* Push all arenas to the free list, except save_arena, which is
     attached to the current thread.  */
  if (save_arena != NULL)
    ((mstate) save_arena)->attached_threads = 1;
  free_list = NULL;
  for (ar_ptr = &main_arena;; )
    {
      mutex_init (&ar_ptr->mutex);
      if (ar_ptr != save_arena)
        {
	  /* This arena is no longer attached to any thread.  */
	  ar_ptr->attached_threads = 0;
          ar_ptr->next_free = free_list;
          free_list = ar_ptr;
        }
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
  mutex_init (&list_lock);
  atfork_recursive_cntr = 0;
}

# else

#  define ptmalloc_unlock_all2 ptmalloc_unlock_all
# endif
#endif  /* !NO_THREADS */

/* Initialization routine. */
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


#ifdef SHARED
static void *
__failing_morecore (ptrdiff_t d)
{
  return (void *) MORECORE_FAILURE;
}

extern struct dl_open_hook *_dl_open_hook;
libc_hidden_proto (_dl_open_hook);
#endif

static void
ptmalloc_init (void)
{
  if (__malloc_initialized >= 0)
    return;

  __malloc_initialized = 0;

#ifdef SHARED
  /* In case this libc copy is in a non-default namespace, never use brk.
     Likewise if dlopened from statically linked program.  */
  Dl_info di;
  struct link_map *l;

  if (_dl_open_hook != NULL
      || (_dl_addr (ptmalloc_init, &di, &l, NULL) != 0
          && l->l_ns != LM_ID_BASE))
    __morecore = __failing_morecore;
#endif

  thread_arena = &main_arena;
  thread_atfork (ptmalloc_lock_all, ptmalloc_unlock_all, ptmalloc_unlock_all2);
  const char *s = NULL;
  if (__glibc_likely (_environ != NULL))
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
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "TOP_PAD_", 8) == 0)
                    __libc_mallopt (M_TOP_PAD, atoi (&envline[9]));
                  else if (memcmp (envline, "PERTURB_", 8) == 0)
                    __libc_mallopt (M_PERTURB, atoi (&envline[9]));
                }
              break;
            case 9:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "MMAP_MAX_", 9) == 0)
                    __libc_mallopt (M_MMAP_MAX, atoi (&envline[10]));
                  else if (memcmp (envline, "ARENA_MAX", 9) == 0)
                    __libc_mallopt (M_ARENA_MAX, atoi (&envline[10]));
                }
              break;
            case 10:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "ARENA_TEST", 10) == 0)
                    __libc_mallopt (M_ARENA_TEST, atoi (&envline[11]));
                }
              break;
            case 15:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "TRIM_THRESHOLD_", 15) == 0)
                    __libc_mallopt (M_TRIM_THRESHOLD, atoi (&envline[16]));
                  else if (memcmp (envline, "MMAP_THRESHOLD_", 15) == 0)
                    __libc_mallopt (M_MMAP_THRESHOLD, atoi (&envline[16]));
                }
              break;
            default:
              break;
            }
        }
    }
  if (s && s[0])
    {
      __libc_mallopt (M_CHECK_ACTION, (int) (s[0] - '0'));
      if (check_action != 0)
        __malloc_check_init ();
    }
  void (*hook) (void) = atomic_forced_read (__malloc_initialize_hook);
  if (hook != NULL)
    (*hook)();
  __malloc_initialized = 1;
}

/* There are platforms (e.g. Hurd) with a link-time hook mechanism. */
#ifdef thread_atfork_static
thread_atfork_static (ptmalloc_lock_all, ptmalloc_unlock_all,		      \
                      ptmalloc_unlock_all2)
#endif



/* Managing heaps and arenas (for concurrent threads) */

#if MALLOC_DEBUG > 1

/* Print the complete contents of a single heap to stderr. */

static void
dump_heap (heap_info *heap)
{
  char *ptr;
  mchunkptr p;
  mchunkptr topchunk;

  fprintf (stderr, "Heap %p, size %10lx:\n", heap, (long) heap->size);
  ptr = (heap->ar_ptr != (mstate) (heap + 1)) ?
        (char *) (heap + 1) : (char *) (heap + 1) + sizeof (struct malloc_state);
  p = (mchunkptr) (((unsigned long) ptr + MALLOC_ALIGN_MASK) &
                   ~MALLOC_ALIGN_MASK);
  for (;; )
    {
      fprintf (stderr, "chunk %p size %10lx", p, (long) p->size);
      topchunk = chunkinfo2chunk((heap->ar_ptr)->_md_top);
      if (p == topchunk)
        {
          fprintf (stderr, " (top)\n");
          break;
        }
      else if (p->size == (0 | PREV_INUSE))
        {
          fprintf (stderr, " (fence)\n");
          break;
        }
      fprintf (stderr, "\n");
      p = next_chunk (p);
    }
}
#endif /* MALLOC_DEBUG > 1 */

/* If consecutive mmap (0, HEAP_MAX_SIZE << 1, ...) calls return decreasing
   addresses as opposed to increasing, new_heap would badly fragment the
   address space.  In that case remember the second HEAP_MAX_SIZE part
   aligned to HEAP_MAX_SIZE from last mmap (0, HEAP_MAX_SIZE << 1, ...)
   call (if it is already aligned) and try to reuse it next time.  We need
   no locking for it, as kernel ensures the atomicity for us - worst case
   we'll call mmap (addr, HEAP_MAX_SIZE, ...) for some value of addr in
   multiple threads, but only one will succeed.  */
static char *aligned_heap_area;

/* Create a new heap.  size is automatically rounded up to a multiple
   of the page size. */

static heap_info *
internal_function
new_heap (size_t size, size_t top_pad)
{
  size_t pagesize = GLRO (dl_pagesize);
  char *p1, *p2;
  unsigned long ul;
  heap_info *h;

  if (size + top_pad < HEAP_MIN_SIZE)
    size = HEAP_MIN_SIZE;
  else if (size + top_pad <= HEAP_MAX_SIZE)
    size += top_pad;
  else if (size > HEAP_MAX_SIZE)
    return 0;
  else
    size = HEAP_MAX_SIZE;
  size = ALIGN_UP (size, pagesize);

  /* A memory region aligned to a multiple of HEAP_MAX_SIZE is needed.
     No swap space needs to be reserved for the following large
     mapping (on Linux, this is the case for all non-writable mappings
     anyway). */
  p2 = MAP_FAILED;
  if (aligned_heap_area)
    {
      p2 = (char *) sys_MMAP (aligned_heap_area, HEAP_MAX_SIZE, PROT_NONE,
                          MAP_NORESERVE);
      aligned_heap_area = NULL;
      if (p2 != MAP_FAILED && ((unsigned long) p2 & (HEAP_MAX_SIZE - 1)))
        {
          __munmap (p2, HEAP_MAX_SIZE);
          p2 = MAP_FAILED;
        }
    }
  if (p2 == MAP_FAILED)
    {
      p1 = (char *) sys_MMAP (0, HEAP_MAX_SIZE << 1, PROT_NONE, MAP_NORESERVE);
      if (p1 != MAP_FAILED)
        {
          p2 = (char *) (((unsigned long) p1 + (HEAP_MAX_SIZE - 1))
                         & ~(HEAP_MAX_SIZE - 1));
          ul = p2 - p1;
          if (ul)
            __munmap (p1, ul);
          else
            aligned_heap_area = p2 + HEAP_MAX_SIZE;
          __munmap (p2 + HEAP_MAX_SIZE, HEAP_MAX_SIZE - ul);
        }
      else
        {
          /* Try to take the chance that an allocation of only HEAP_MAX_SIZE
             is already aligned. */
          p2 = (char *) sys_MMAP (0, HEAP_MAX_SIZE, PROT_NONE, MAP_NORESERVE);
          if (p2 == MAP_FAILED)
            return 0;

          if ((unsigned long) p2 & (HEAP_MAX_SIZE - 1))
            {
              __munmap (p2, HEAP_MAX_SIZE);
              return 0;
            }
        }
    }
  if (__mprotect (p2, size, PROT_READ | PROT_WRITE) != 0)
    {
      __munmap (p2, HEAP_MAX_SIZE);
      return 0;
    }
  h = (heap_info *) p2;
  h->size = size;
  h->mprotect_size = size;
  LIBC_PROBE (memory_heap_new, 2, h, h->size);
  return h;
}

/* Grow a heap.  size is automatically rounded up to a
   multiple of the page size. */

static int
grow_heap (heap_info *h, long diff)
{
  size_t pagesize = GLRO (dl_pagesize);
  long new_size;

  diff = ALIGN_UP (diff, pagesize);
  new_size = (long) h->size + diff;
  if ((unsigned long) new_size > (unsigned long) HEAP_MAX_SIZE)
    return -1;

  if ((unsigned long) new_size > h->mprotect_size)
    {
      if (__mprotect ((char *) h + h->mprotect_size,
                      (unsigned long) new_size - h->mprotect_size,
                      PROT_READ | PROT_WRITE) != 0)
        return -2;

      h->mprotect_size = new_size;
    }

  h->size = new_size;
  LIBC_PROBE (memory_heap_more, 2, h, h->size);
  return 0;
}

/* Shrink a heap.  */

static int
shrink_heap (heap_info *h, long diff)
{
  long new_size;

  new_size = (long) h->size - diff;
  if (new_size < (long) sizeof (*h))
    return -1;

  /* Try to re-map the extra heap space freshly to save memory, and make it
     inaccessible.  See malloc-sysdep.h to know when this is true.  */
  if (__glibc_unlikely (check_may_shrink_heap ()))
    {
      if ((char *) sys_MMAP ((char *) h + new_size, diff, PROT_NONE, MAP_FIXED) == (char *) MAP_FAILED)
        return -2;

      h->mprotect_size = new_size;
    }
  else
    __madvise ((char *) h + new_size, diff, MADV_DONTNEED);
  /*fprintf(stderr, "shrink %p %08lx\n", h, new_size);*/

  h->size = new_size;
  LIBC_PROBE (memory_heap_less, 2, h, h->size);
  return 0;
}

/* Delete a heap. */

#define delete_heap(heap) \
  do {									      \
      if ((char *) (heap) + HEAP_MAX_SIZE == aligned_heap_area)		      \
        aligned_heap_area = NULL;					      \
      __munmap ((char *) (heap), HEAP_MAX_SIZE);			      \
    } while (0)

static int
internal_function
heap_trim (heap_info *heap, size_t pad)
{
  mstate ar_ptr = heap->ar_ptr;
  unsigned long pagesz = GLRO (dl_pagesize);
  mchunkptr top_chunk = chunkinfo2chunk(ar_ptr->_md_top);

  chunkinfoptr bck, fwd;


  mchunkptr p;
  chunkinfoptr _md_temp;
  chunkinfoptr _md_p;
  chunkinfoptr _md_fencepost;

  heap_info *prev_heap;
  long new_size, top_size, top_area, extra, prev_size, misalign;

  /* Can this heap go away completely? */
  /*
   * SRI: the mstate part of the header only occurs in the first heap
   * of this arena. all subsequent heaps in the arena start out
   * with their top being the next pointer after the heap_info
   * header.
   *
   * so this condition is asking: is this non-first heap of
   * this arena empty.
  */
  while (top_chunk == chunk_at_offset (heap, sizeof (*heap)))
    {

      do_check_top(ar_ptr, __FILE__, __LINE__);

      /* 
       * SRI: note that we know prev_heap is not null,
       * because we are in a non-first heap of this arena. 
       */
      prev_heap = heap->prev;
      prev_size = prev_heap->size - (MINSIZE - 2 * SIZE_SZ);

      /* SRI: we are going to delete this heap and consolidate the tail of the previous heap */
      p = chunk_at_offset (prev_heap, prev_size);
 

      /* fencepost must be properly aligned.  */
      misalign = ((long) p) & MALLOC_ALIGN_MASK;
      p = chunk_at_offset (prev_heap, prev_size - misalign);
      _md_p = lookup_chunk(ar_ptr, p);

      if(_md_p == NULL){
	missing_metadata(ar_ptr, p);
	return 0;
      }
      assert (_md_p->size == (0 | PREV_INUSE)); /* must be fencepost_0 */
      
      _md_fencepost = _md_p;

      p = prev_chunk (_md_p, p);
      _md_p = lookup_chunk(ar_ptr, p);
      if(_md_p == NULL){
	missing_metadata(ar_ptr, p); 
	return 0;
      }
 
      new_size = chunksize (_md_p) + (MINSIZE - 2 * SIZE_SZ) + misalign;

      assert (new_size > 0 && new_size < (long) (2 * MINSIZE)); /* must be fencepost_1 */

      if (!prev_inuse(_md_p, p)){
        new_size += _md_p->prev_size;
      }
      assert (new_size > 0 && new_size < HEAP_MAX_SIZE);

      if (new_size + (HEAP_MAX_SIZE - prev_heap->size) < pad + MINSIZE + pagesz){
        break;  /* SRI: are we in a sane state here? */
      }

      _md_temp = _md_fencepost->md_next;
      
      /* SRI: pulling out the fencepost */
      unregister_chunk(ar_ptr, chunkinfo2chunk(_md_fencepost), 4); 
      
      /* fix the md_next and md_prev pointers */
      _md_p->md_next = _md_temp;
      if(_md_temp != NULL)
	_md_temp->md_prev = _md_p;
      
      ar_ptr->system_mem -= heap->size;
      arena_mem -= heap->size;
      LIBC_PROBE (memory_heap_free, 2, heap, heap->size);
      delete_heap (heap);
      lookup_delete_heap(heap);
      heap = prev_heap;

      if (!prev_inuse(_md_p, p)) /* consolidate backward; SRI: size already done above */
        {
	  mchunkptr op = p;
	  _md_temp = _md_p->md_next;
          p = prev_chunk (_md_p, p);
	  unregister_chunk(ar_ptr, op, 5);  
	  //FIXME: once twinned we can use the md_prev pointer here.
	  _md_p = lookup_chunk(ar_ptr, p);
	  if(_md_p == NULL){
	    missing_metadata(ar_ptr, p);
	    return 0;
	  }
          bin_unlink(ar_ptr, _md_p, &bck, &fwd);
	  /* fix the md_next and md_prev pointers */
	  _md_p->md_next = _md_temp;
	  if(_md_temp != NULL)
	    _md_temp->md_prev = _md_p;
        }
      assert (((unsigned long) ((char *) p + new_size) & (pagesz - 1)) == 0);
      assert (((char *) p + new_size) == ((char *) heap + heap->size));
      top_chunk = p;
      ar_ptr->_md_top = _md_p;
      set_head (_md_p, new_size | PREV_INUSE);

      do_check_top(ar_ptr, __FILE__, __LINE__);

      /* check_chunk(ar_ptr, top_chunk); */
    } /* while */

  /* Uses similar logic for per-thread arenas as the main arena with systrim
     and _int_free by preserving the top pad and rounding down to the nearest
     page.  */
  top_size = chunksize (ar_ptr->_md_top);
  if ((unsigned long)(top_size) <
      (unsigned long)(mp_.trim_threshold))
    return 0;

  top_area = top_size - MINSIZE - 1;
  if (top_area < 0 || (size_t) top_area <= pad)
    return 0;

  /* Release in pagesize units and round down to the nearest page.  */
  extra = ALIGN_DOWN(top_area - pad, pagesz);
  if (extra == 0)
    return 0;

  /* Try to shrink. */
  if (shrink_heap (heap, extra) != 0)
    return 0;

  ar_ptr->system_mem -= extra;
  arena_mem -= extra;

  /* Success. Adjust top accordingly. */
  set_head (ar_ptr->_md_top, (top_size - extra) | PREV_INUSE);

  do_check_top(ar_ptr, __FILE__, __LINE__);

  /*check_chunk(ar_ptr, top_chunk);*/
  return 1;
}


/* Create a new arena with initial size "size".  */

/* If REPLACED_ARENA is not NULL, detach it from this thread.  Must be
   called while list_lock is held.  */
static void
detach_arena (mstate replaced_arena)
{
  if (replaced_arena != NULL)
    {
      assert (replaced_arena->attached_threads > 0);
      /* The current implementation only detaches from main_arena in
	 case of allocation failure.  This means that it is likely not
	 beneficial to put the arena on free_list even if the
	 reference count reaches zero.  */
      --replaced_arena->attached_threads;
    }
}

static mstate
_int_new_arena (size_t size)
{
  mstate a;
  heap_info *h;
  char *ptr;
  unsigned long misalign;

  h = new_heap (size + (sizeof (*h) + sizeof (*a) + MALLOC_ALIGNMENT),
                mp_.top_pad);
  if (!h)
    {
      /* Maybe size is too large to fit in a single heap.  So, just try
         to create a minimally-sized arena and let _int_malloc() attempt
         to deal with the large request via mmap_chunk().  */
      h = new_heap (sizeof (*h) + sizeof (*a) + MALLOC_ALIGNMENT, mp_.top_pad);
      if (!h)
        return 0;
    }
  a = h->ar_ptr = (mstate) (h + 1);
  malloc_init_state (a, false);
  a->attached_threads = 1;
  /*a->next = NULL;*/
  a->system_mem = a->max_system_mem = h->size;
  arena_mem += h->size;

  /* Set up the top chunk, with proper alignment. */
  ptr = (char *) (a + 1);
  misalign = (unsigned long) chunk2mem (ptr) & MALLOC_ALIGN_MASK;
  if (misalign > 0)
    ptr += MALLOC_ALIGNMENT - misalign;
  a->_md_top = register_chunk(a, (mchunkptr) ptr, false, 18);
  //Done a->_md_top->md_prev = a->_md_top->md_next = NULL
  set_head (a->_md_top, (((char *) h + h->size) - ptr) | PREV_INUSE);

  do_check_top(a, __FILE__, __LINE__);

  LIBC_PROBE (memory_arena_new, 2, a, size);
  mstate replaced_arena = thread_arena;
  thread_arena = a;
  mutex_init (&a->mutex);
  //BD moved this so we could call the LOCK_ARENA below.
  //(void) mutex_lock (&a->mutex);

  (void) mutex_lock (&list_lock);

  detach_arena (replaced_arena);

  /* Add the new arena to the "end" of the global cyclic list.  */
  a->next = &main_arena;

  last_arena->next = a;

  catomic_increment (&arena_count);

  atomic_write_barrier ();

  last_arena = a;

  /* update the index of the new arena */
  a->arena_index = arena_count;

  lookup_add_heap(h, a->arena_index);

  set_arena_index(a, chunkinfo2chunk(a->_md_top), arena_count);
  LOCK_ARENA(a, ARENA_SITE);

  (void) mutex_unlock (&list_lock);

  return a;
}


static mstate
get_free_list (void)
{
  mstate replaced_arena = thread_arena;
  mstate result = free_list;
  if (result != NULL)
    {
      (void) mutex_lock (&list_lock);
      result = free_list;
      if (result != NULL)
	{
	  free_list = result->next_free;

	  /* Arenas on the free list are not attached to any thread.  */
	  assert (result->attached_threads == 0);
	  /* But the arena will now be attached to this thread.  */
	  result->attached_threads = 1;

	  detach_arena (replaced_arena);
	}
      (void) mutex_unlock (&list_lock);
      if (result != NULL)
        {
          LIBC_PROBE (memory_arena_reuse_free_list, 1, result);
	  LOCK_ARENA(result, ARENA_SITE);
	  thread_arena = result;
        }
    }

  return result;
}

/* Lock and return an arena that can be reused for memory allocation.
   Avoid AVOID_ARENA as we have already failed to allocate memory in
   it and it is currently locked.  */
static mstate
reused_arena (mstate avoid_arena)
{
  mstate result;
  static mstate next_to_use;

  int success;

  if (next_to_use == NULL)
    next_to_use = &main_arena;

  result = next_to_use;
  do
    {
      if (!arena_is_corrupt (result))
	  success = mutex_trylock (&result->mutex);
	  if (!success) {
	    goto out;
	  }
      result = result->next;
    }
  while (result != next_to_use);

  /* Avoid AVOID_ARENA as we have already failed to allocate memory
     in that arena and it is currently locked.   */
  if (result == avoid_arena)
    result = result->next;

  /* Make sure that the arena we get is not corrupted.  */
  mstate begin = result;
  while (arena_is_corrupt (result) || result == avoid_arena)
    {
      result = result->next;
      if (result == begin)
	break;
    }

  /* We could not find any arena that was either not corrupted or not the one
     we wanted to avoid.  */
  if (result == begin || result == avoid_arena)
    return NULL;

  /* No arena available without contention.  Wait for the next in line.  */
  LIBC_PROBE (memory_arena_reuse_wait, 3, &result->mutex, result, avoid_arena);
  LOCK_ARENA(result, ARENA_SITE);

out:
  {
    mstate replaced_arena = thread_arena;
    (void) mutex_lock (&list_lock);
    detach_arena (replaced_arena);
    ++result->attached_threads;
  (void) mutex_unlock (&list_lock);
  }

  LIBC_PROBE (memory_arena_reuse, 2, result, avoid_arena);
  thread_arena = result;
  next_to_use = result->next;

  return result;
}

static mstate
internal_function
arena_get2 (size_t size, mstate avoid_arena)
{
  mstate a;

  static size_t narenas_limit;

  a = get_free_list ();
  if (a == NULL)
    {
      /* Nothing immediately available, so generate a new arena.  */
      if (narenas_limit == 0)
        {
          if (mp_.arena_max != 0)
            narenas_limit = mp_.arena_max;
          else if (narenas > mp_.arena_test)
            {
              int n = __get_nprocs ();

              if (n >= 1)
                narenas_limit = NARENAS_FROM_NCORES (n);
              else
                /* We have no information about the system.  Assume two
                   cores.  */
                narenas_limit = NARENAS_FROM_NCORES (2);
            }
        }
    repeat:;
      size_t n = narenas;
      /* NB: the following depends on the fact that (size_t)0 - 1 is a
         very large number and that the underflow is OK.  If arena_max
         is set the value of arena_test is irrelevant.  If arena_test
         is set but narenas is not yet larger or equal to arena_test
         narenas_limit is 0.  There is no possibility for narenas to
         be too big for the test to always fail since there is not
         enough address space to create that many arenas.  */
      if (__glibc_unlikely (n <= narenas_limit - 1))
        {
          if (catomic_compare_and_exchange_bool_acq (&narenas, n + 1, n))
            goto repeat;
          a = _int_new_arena (size);
	  if (__glibc_unlikely (a == NULL))
            catomic_decrement (&narenas);
        }
      else
        a = reused_arena (avoid_arena);
    }
  return a;
}

/* If we don't have the main arena, then maybe the failure is due to running
   out of mmapped areas, so we can try allocating on the main arena.
   Otherwise, it is likely that sbrk() has failed and there is still a chance
   to mmap(), so try one of the other arenas.  */
static mstate
arena_get_retry (mstate ar_ptr, size_t bytes, int site)
{
  LIBC_PROBE (memory_arena_retry, 2, bytes, ar_ptr);
  if (ar_ptr != &main_arena)
    {
      UNLOCK_ARENA(ar_ptr, site);
      /* Don't touch the main arena if it is corrupt.  */
      if (arena_is_corrupt (&main_arena))
	return NULL;

      ar_ptr = &main_arena;
      LOCK_ARENA(ar_ptr, site);
    }
  else
    {
      UNLOCK_ARENA(ar_ptr, site);
      ar_ptr = arena_get2 (bytes, ar_ptr);
    }

  return ar_ptr;
}

static void __attribute__ ((section ("__libc_thread_freeres_fn")))
arena_thread_freeres (void)
{
  mstate a = thread_arena;
  thread_arena = NULL;

  if (a != NULL)
    {
      (void) mutex_lock (&list_lock);

      /* If this was the last attached thread for this arena, put the
	 arena on the free list.  */
      assert (a->attached_threads > 0);
      if (--a->attached_threads == 0)
	{
	  a->next_free = free_list;
	  free_list = a;
	}
      (void) mutex_unlock (&list_lock);
    }
}
text_set_element (__libc_thread_subfreeres, arena_thread_freeres);

/*
 * Local variables:
 * c-basic-offset: 2
 * End:
 */
