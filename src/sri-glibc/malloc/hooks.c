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

/* What to do if the standard debugging hooks are in place and a
   corrupt pointer is detected: do nothing (0), print an error message
   (1), or call abort() (2). */

/* Hooks for debugging versions.  The initial hooks just call the
   initialization routine, then do the normal work. */

static void *
malloc_hook_ini (size_t sz, const void *caller)
{
  __malloc_hook = NULL;
  ptmalloc_init ();
  return __libc_malloc (sz);
}

static void *
realloc_hook_ini (void *ptr, size_t sz, const void *caller)
{
  __malloc_hook = NULL;
  __realloc_hook = NULL;
  ptmalloc_init ();
  return __libc_realloc (ptr, sz);
}

static void *
memalign_hook_ini (size_t alignment, size_t sz, const void *caller)
{
  __memalign_hook = NULL;
  ptmalloc_init ();
  return __libc_memalign (alignment, sz);
}

/* Whether we are using malloc checking.  */
static int using_malloc_checking;

/* A flag that is set by malloc_set_state, to signal that malloc checking
   must not be enabled on the request from the user (via the MALLOC_CHECK_
   environment variable).  It is reset by __malloc_check_init to tell
   malloc_set_state that the user has requested malloc checking.

   The purpose of this flag is to make sure that malloc checking is not
   enabled when the heap to be restored was constructed without malloc
   checking, and thus does not contain the required magic bytes.
   Otherwise the heap would be corrupted by calls to free and realloc.  If
   it turns out that the heap was created with malloc checking and the
   user has requested it malloc_set_state just calls __malloc_check_init
   again to enable it.  On the other hand, reusing such a heap without
   further malloc checking is safe.  */
static int disallow_malloc_check;

/* Activate a standard set of debugging hooks. */
void
__malloc_check_init (void)
{
  if (disallow_malloc_check)
    {
      disallow_malloc_check = 0;
      return;
    }
  using_malloc_checking = 1;
  __malloc_hook = malloc_check;
  __free_hook = free_check;
  __realloc_hook = realloc_check;
  __memalign_hook = memalign_check;
}

/* A simple, standard set of debugging hooks.  Overhead is `only' one
   byte per chunk; still this will catch most cases of double frees or
   overruns.  The goal here is to avoid obscure crashes due to invalid
   usage, unlike in the MALLOC_DEBUG code.
*/
static unsigned char
magicbyte (const void *p)
{
  unsigned char magic;

  magic = (((uintptr_t) p >> 3) ^ ((uintptr_t) p >> 11)) & 0xFF;
  // Do not return 1.  See the comment in mem2mem_check().  
  if (magic == 1)
    ++magic;
  return magic;
}


/* Visualize the chunk as being partitioned into blocks of 255 bytes from the
   highest address of the chunk, downwards.  The end of each block tells
   us the size of that block, up to the actual size of the requested
   memory.  Our magic byte is right at the end of the requested size, so we
   must reach it with this iteration, otherwise we have witnessed a memory
   corruption.  */
static size_t
malloc_check_get_size (mstate av, chunkinfoptr _md_p, mchunkptr p)
{
  size_t size;
  unsigned char c;
  unsigned char magic = magicbyte (p);

  assert (using_malloc_checking == 1);

  for (size = chunksize (_md_p) - 1 + (chunk_is_mmapped (_md_p, p) ? 0 : SIZE_SZ);
       (c = ((unsigned char *) p)[size]) != magic;
       size -= c)
    {
      if (c <= 0 || size < (c + 2 * SIZE_SZ))
        {
          malloc_printerr (check_action, "malloc_check_get_size: memory corruption",
                           chunk2mem (p),
			   chunk_is_mmapped (_md_p, p) ? &main_arena : av);
          return 0;
        }
    }
  
  // chunk2mem size. 
  return size - 2 * SIZE_SZ;
}

/* Instrument a chunk with overrun detector byte(s) and convert it
   into a user pointer with requested size req_sz. */

static void *
internal_function
mem2mem_check (chunkinfoptr _md_p, mchunkptr p, void *mem, size_t req_sz)
{
  unsigned char *m_ptr = mem;
  size_t max_sz, block_sz, i;
  unsigned char magic;

  if (!mem)
    return mem;

  assert(p == mem2chunk (mem));

  magic = magicbyte (p);
  max_sz = chunksize (_md_p) - 2 * SIZE_SZ;
  if (!chunk_is_mmapped (_md_p, p))
    max_sz += SIZE_SZ;
  for (i = max_sz - 1; i > req_sz; i -= block_sz)
    {
      block_sz = MIN (i - req_sz, 0xff);
      // Don't allow the magic byte to appear in the chain of length bytes.
      //   For the following to work, magicbyte cannot return 0x01.  
      if (block_sz == magic)
        --block_sz;

      m_ptr[i] = block_sz;
    }
  m_ptr[req_sz] = magic;
  return (void *) m_ptr;
}

/* Convert a pointer to be free()d or realloc()ed to a valid chunk
   pointer.  If the provided pointer is not valid, return NULL. */

static mchunkptr
internal_function
mem2chunk_check (chunkinfoptr _md_p, mchunkptr p, void *mem, unsigned char **magic_p)
{
  INTERNAL_SIZE_T sz, c;
  unsigned char magic;
  mchunkptr prev_p;
  mchunkptr next_p;
  chunkinfoptr _md_prev_p;

  if (!aligned_OK ((unsigned long)mem))
    return NULL;

  assert(p == mem2chunk (mem));
  sz = chunksize (_md_p);
  magic = magicbyte (p);
  if (!chunk_is_mmapped (_md_p, p))
    {
      // Must be a chunk in conventional heap memory. 
      int contig = contiguous (&main_arena);

      if (contig &&
	  ((char *) p < mp_.sbrk_base ||
	   ((char *) p + sz) >= (mp_.sbrk_base + main_arena.system_mem))){
	return NULL;
      }

      if(sz < MINSIZE || sz & MALLOC_ALIGN_MASK || !inuse (&main_arena, _md_p, p)){
	return NULL;
      }
      
      if( !prev_inuse (_md_p, p) ){
	if(_md_p->prev_size & MALLOC_ALIGN_MASK){
	  return NULL;
	}
	prev_p = prev_chunk(_md_p, p);
	if(contig){
	  if ((char*)prev_p < mp_.sbrk_base){ return NULL; }
	}

	assert(md_prev_sanity_check(&main_arena, _md_p, p));

	_md_prev_p = _md_p->md_prev;

	next_p = next_chunk(_md_prev_p, prev_p);
	if (next_p != p){
	  return NULL;
	}
      }

      for (sz += SIZE_SZ - 1; (c = ((unsigned char *) p)[sz]) != magic; sz -= c)
        {
          if (c == 0 || sz < (c + 2 * SIZE_SZ))
            return NULL;
        }
    }
  else
    {
      unsigned long offset, page_mask = GLRO (dl_pagesize) - 1;

      // mmap()ed chunks have MALLOC_ALIGNMENT or higher power-of-two
      //   alignment relative to the beginning of a page.  Check this
      //   first. 
      offset = (unsigned long) mem & page_mask;
      if ((offset != MALLOC_ALIGNMENT && offset != 0 && offset != 0x10 &&
           offset != 0x20 && offset != 0x40 && offset != 0x80 && offset != 0x100 &&
           offset != 0x200 && offset != 0x400 && offset != 0x800 && offset != 0x1000 &&
           offset < 0x2000) ||
          !chunk_is_mmapped (_md_p, p) || (_md_p->size & PREV_INUSE) ||
          ((((unsigned long) p - _md_p->prev_size) & page_mask) != 0) ||
          ((_md_p->prev_size + sz) & page_mask) != 0)
        return NULL;

      for (sz -= 1; (c = ((unsigned char *) p)[sz]) != magic; sz -= c)
        {
          if (c == 0 || sz < (c + 2 * SIZE_SZ))
            return NULL;
        }
    }
  ((unsigned char *) p)[sz] ^= 0xFF;
  if (magic_p)
    *magic_p = (unsigned char *) p + sz;
  return p;
}

/* Check for corruption of the top chunk, and try to recover if
   necessary. */

static int
internal_function
top_check (void)
{
  chunkinfoptr _md_ot = main_arena._md_top;
  mchunkptr ot = chunkinfo2chunk(_md_ot);
  INTERNAL_SIZE_T ot_sz = chunksize(_md_ot);
  char *brk, *new_brk;
  INTERNAL_SIZE_T front_misalign, sbrk_size;
  mchunkptr topchunk;
  unsigned long pagesz = GLRO (dl_pagesize);

  if (ot == &main_arena.initial_top ||
      (!chunk_is_mmapped (_md_ot, ot) &&
       ot_sz >= MINSIZE &&
       prev_inuse (_md_ot, ot) &&
       (!contiguous (&main_arena) ||
        (char *) ot + ot_sz == mp_.sbrk_base + main_arena.system_mem)))
    return 0;

  malloc_printerr (check_action, "malloc: top chunk is corrupt", ot,
		   &main_arena);

  // Try to set up a new top chunk. 
  brk = MORECORE (0);
  front_misalign = (unsigned long) chunk2mem (brk) & MALLOC_ALIGN_MASK;
  if (front_misalign > 0)
    front_misalign = MALLOC_ALIGNMENT - front_misalign;
  sbrk_size = front_misalign + mp_.top_pad + MINSIZE;
  sbrk_size += pagesz - ((unsigned long) (brk + sbrk_size) & (pagesz - 1));
  new_brk = (char *) (MORECORE (sbrk_size));
  if (new_brk == (char *) (MORECORE_FAILURE))
    {
      __set_errno (ENOMEM);
      return -1;
    }
  // Call the `morecore' hook if necessary.  
  void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
  if (hook)
    (*hook)();
  main_arena.system_mem = (new_brk - mp_.sbrk_base) + sbrk_size;
  
  topchunk = (mchunkptr) (brk + front_misalign);
  main_arena._md_top = register_chunk(&main_arena, topchunk, false, 19);
  //Done: main_arena._md_top->md_prev = main_arena._md_top->md_next = NULL
  set_head (main_arena._md_top, (sbrk_size - front_misalign) | PREV_INUSE);

  return 0;
}

static void *
malloc_check (size_t sz, const void *caller)
{
  chunkinfoptr _md_victim;
  void *mem;

  if (sz + 1 == 0)
    {
      __set_errno (ENOMEM);
      return NULL;
    }

  (void) mutex_lock (&main_arena.mutex);
  _md_victim = (top_check () >= 0) ? _int_malloc (&main_arena, sz + 1) : NULL;
  (void) mutex_unlock (&main_arena.mutex);
  mem = chunkinfo2mem(_md_victim);
  return mem2mem_check (_md_victim, chunkinfo2chunk(_md_victim), mem, sz);
}

static void
free_check (void *mem, const void *caller)
{
  mchunkptr p;
  chunkinfoptr _md_p;


  if (!mem)
    return;

  (void) mutex_lock (&main_arena.mutex);
  p = mem2chunk(mem);
  _md_p = lookup_chunk(&main_arena, p);
  p = mem2chunk_check (_md_p, p, mem, NULL);
  if (!p || !_md_p)
    {
      (void) mutex_unlock (&main_arena.mutex);

      malloc_printerr (check_action, "free(): invalid pointer", mem,
		       &main_arena);
      return;
    }

  if (chunk_is_mmapped (_md_p, p))
    {
      munmap_chunk(_md_p);
      unregister_chunk(&main_arena, p, false); 
      //Done: _md_p->md_prev = _md_p->md_next = NULL
      (void) mutex_unlock (&main_arena.mutex);
      return;
    }
  _int_free (&main_arena, NULL, p, true, false);
  (void) mutex_unlock (&main_arena.mutex);
}

static void *
realloc_check (void *oldmem, size_t bytes, const void *caller)
{
  INTERNAL_SIZE_T nb;
  void *newmem = 0;
  chunkinfoptr _md_newmem;
  unsigned char *magic_p;
  mchunkptr oldp;
  chunkinfoptr _md_oldp;

  _md_newmem = NULL;

  if (bytes + 1 == 0)
    {
      __set_errno (ENOMEM);
      return NULL;
    }
  if (oldmem == 0)
    return malloc_check (bytes, NULL);

  if (bytes == 0)
    {
      free_check (oldmem, NULL);
      return NULL;
    }
  (void) mutex_lock (&main_arena.mutex);
  oldp = mem2chunk(oldmem);
  _md_oldp = lookup_chunk(&main_arena, oldp);
  if( !_md_oldp){
    return NULL;
  }
  oldp = mem2chunk_check (_md_oldp,  oldp, oldmem, &magic_p);

  (void) mutex_unlock (&main_arena.mutex);
  if (!oldp)
    {
      malloc_printerr (check_action, "realloc(): invalid pointer", oldmem,
		       &main_arena);
      return malloc_check (bytes, NULL);
    }
  const INTERNAL_SIZE_T oldsize = chunksize (_md_oldp);

  if ( !checked_request2size (bytes + 1, &nb) ){
    return 0;
  }
  (void) mutex_lock (&main_arena.mutex);

  if (chunk_is_mmapped (_md_oldp, oldp))
    {

#if HAVE_MREMAP
      mchunkptr newp = mremap_chunk (&main_arena, _md_oldp, nb);
      if (newp)
        newmem = chunk2mem (newp);
      else
#endif
      {
        /* Note the extra SIZE_SZ overhead. */
        if (oldsize - SIZE_SZ >= nb)
          newmem = oldmem; /* do nothing */
        else
          {
            /* Must alloc, copy, free. */
            if (top_check () >= 0){
              _md_newmem = _int_malloc (&main_arena, bytes + 1);
	      newmem = chunkinfo2mem(_md_newmem);
	    }
            if (newmem)
              {
                memcpy (newmem, oldmem, oldsize - 2 * SIZE_SZ);
                munmap_chunk (_md_oldp);
		unregister_chunk(&main_arena, oldp, false);
		//Done: _md_p->md_prev = _md_p->md_next = NULL
              }
          }
      }
    }
  else
    {
      if (top_check () >= 0)
        {
          INTERNAL_SIZE_T nb;
	  if ( !checked_request2size (bytes + 1, &nb) ){
	    return 0;
	  }
          _md_newmem = _int_realloc (&main_arena, _md_oldp, oldsize, nb);
	  newmem = chunkinfo2mem(_md_newmem);
        }
    }

  /* mem2chunk_check changed the magic byte in the old chunk.
     If newmem is NULL, then the old chunk will still be used though,
     so we need to invert that change here.  */
  if (newmem == NULL)
    *magic_p ^= 0xFF;

  (void) mutex_unlock (&main_arena.mutex);

  return _md_newmem ? mem2mem_check (_md_newmem, chunkinfo2chunk(_md_newmem), newmem, bytes) : NULL;
}

static void *
memalign_check (size_t alignment, size_t bytes, const void *caller)
{
  void *mem;
  chunkinfoptr _md_mem;

  if (alignment <= MALLOC_ALIGNMENT)
    return malloc_check (bytes, NULL);

  if (alignment < MINSIZE)
    alignment = MINSIZE;

  /* If the alignment is greater than SIZE_MAX / 2 + 1 it cannot be a
     power of 2 and will cause overflow in the check below.  */
  if (alignment > SIZE_MAX / 2 + 1)
    {
      __set_errno (EINVAL);
      return 0;
    }

  /* Check for overflow.  */
  if (bytes > SIZE_MAX - alignment - MINSIZE)
    {
      __set_errno (ENOMEM);
      return 0;
    }

  /* Make sure alignment is power of 2.  */
  if (!powerof2 (alignment))
    {
      size_t a = MALLOC_ALIGNMENT * 2;
      while (a < alignment)
        a <<= 1;
      alignment = a;
    }

  (void) mutex_lock (&main_arena.mutex);
  _md_mem = (top_check () >= 0) ? _int_memalign (&main_arena, alignment, bytes + 1) :
        NULL;
  mem = chunkinfo2mem(_md_mem);
  (void) mutex_unlock (&main_arena.mutex);
  return mem2mem_check (_md_mem, chunkinfo2chunk(_md_mem), mem, bytes);
}


/* Get/set state: malloc_get_state() records the current state of all
   malloc variables (_except_ for the actual heap contents and `hook'
   function pointers) in a system dependent, opaque data structure.
   This data structure is dynamically allocated and can be free()d
   after use.  malloc_set_state() restores the state of all malloc
   variables to the previously obtained state.  This is especially
   useful when using this malloc as part of a shared library, and when
   the heap contents are saved/restored via some other method.  The
   primary example for this is GNU Emacs with its `dumping' procedure.
   `Hook' function pointers are never saved or restored by these
   functions, with two exceptions: If malloc checking was in use when
   malloc_get_state() was called, then malloc_set_state() calls
   __malloc_check_init() if possible; if malloc checking was not in
   use in the recorded state but the user requested malloc checking,
   then the hooks are reset to 0.  */

#define MALLOC_STATE_MAGIC   0x444c4541l
#define MALLOC_STATE_VERSION (0 * 0x100l + 4l) /* major*0x100 + minor */

struct malloc_save_state
{
  long magic;
  long version;
  mbinptr av[NBINS * 2 + 2];
  char *sbrk_base;
  int sbrked_mem_bytes;
  unsigned long trim_threshold;
  unsigned long top_pad;
  unsigned int n_mmaps_max;
  unsigned long mmap_threshold;
  int check_action;
  unsigned long max_sbrked_mem;
  unsigned long max_total_mem;
  unsigned int n_mmaps;
  unsigned int max_n_mmaps;
  unsigned long mmapped_mem;
  unsigned long max_mmapped_mem;
  int using_malloc_checking;
  unsigned long max_fast;
  unsigned long arena_test;
  unsigned long arena_max;
  unsigned long narenas;
};

void *
__malloc_get_state (void)
{
  struct malloc_save_state *ms;
  int i;
  mbinptr b;

  ms = (struct malloc_save_state *) __libc_malloc (sizeof (*ms));
  if (!ms)
    return 0;

  (void) mutex_lock (&main_arena.mutex);
  malloc_consolidate (&main_arena);
  ms->magic = MALLOC_STATE_MAGIC;
  ms->version = MALLOC_STATE_VERSION;
  ms->av[0] = 0;
  ms->av[1] = 0; /* used to be binblocks, now no longer used */
  ms->av[2] = main_arena._md_top;
  ms->av[3] = 0; /* used to be undefined */
  for (i = 1; i < NBINS; i++)
    {
      b = bin_at (&main_arena, i);
      if (first (b) == b)
        ms->av[2 * i + 2] = ms->av[2 * i + 3] = 0; /* empty bin */
      else
        {
          ms->av[2 * i + 2] = first (b);
          ms->av[2 * i + 3] = last (b);
        }
    }
  ms->sbrk_base = mp_.sbrk_base;
  ms->sbrked_mem_bytes = main_arena.system_mem;
  ms->trim_threshold = mp_.trim_threshold;
  ms->top_pad = mp_.top_pad;
  ms->n_mmaps_max = mp_.n_mmaps_max;
  ms->mmap_threshold = mp_.mmap_threshold;
  ms->check_action = check_action;
  ms->max_sbrked_mem = main_arena.max_system_mem;
  ms->max_total_mem = 0;
  ms->n_mmaps = mp_.n_mmaps;
  ms->max_n_mmaps = mp_.max_n_mmaps;
  ms->mmapped_mem = mp_.mmapped_mem;
  ms->max_mmapped_mem = mp_.max_mmapped_mem;
  ms->using_malloc_checking = using_malloc_checking;
  ms->max_fast = get_max_fast ();
  ms->arena_test = mp_.arena_test;
  ms->arena_max = mp_.arena_max;
  ms->narenas = narenas;
  (void) mutex_unlock (&main_arena.mutex);
  return (void *) ms;
}

int
__malloc_set_state (void *msptr)
{
  struct malloc_save_state *ms = (struct malloc_save_state *) msptr;
  size_t i;
  mbinptr b;

  disallow_malloc_check = 1;
  ptmalloc_init ();
  if (ms->magic != MALLOC_STATE_MAGIC)
    return -1;

  /* Must fail if the major version is too high. */
  if ((ms->version & ~0xffl) > (MALLOC_STATE_VERSION & ~0xffl))
    return -2;

  (void) mutex_lock (&main_arena.mutex);
  /* There are no fastchunks.  */
  clear_fastchunks (&main_arena);
  if (ms->version >= 4)
    set_max_fast (ms->max_fast);
  else
    set_max_fast (64);  /* 64 used to be the value we always used.  */
  for (i = 0; i < NFASTBINS; ++i)
    fastbin (&main_arena, i) = 0;
  for (i = 0; i < BINMAPSIZE; ++i)
    main_arena.binmap[i] = 0;
  main_arena._md_top = ms->av[2];
  main_arena.last_remainder = 0;
  for (i = 1; i < NBINS; i++)
    {
      b = bin_at (&main_arena, i);
      if (ms->av[2 * i + 2] == 0)
        {
          assert (ms->av[2 * i + 3] == 0);
          first (b) = last (b) = b;
        }
      else
        {
          if (ms->version >= 3 &&
              (i < NSMALLBINS || (largebin_index (chunksize (ms->av[2 * i + 2])) == i &&
                                  largebin_index (chunksize (ms->av[2 * i + 3])) == i)))
            {
              first (b) = ms->av[2 * i + 2];
              last (b) = ms->av[2 * i + 3];
              /* Make sure the links to the bins within the heap are correct.  */
              first (b)->bk = b;
              last (b)->fd = b;
              /* Set bit in binblocks.  */
              mark_bin (&main_arena, i);
            }
          else
            {
              /* Oops, index computation from chunksize must have changed.
                 Link the whole list into unsorted_chunks.  */
              first (b) = last (b) = b;
              b = unsorted_chunks (&main_arena);
              ms->av[2 * i + 2]->bk = b;
              ms->av[2 * i + 3]->fd = b->fd;
              b->fd->bk = ms->av[2 * i + 3];
              b->fd = ms->av[2 * i + 2];
            }
        }
    }
  if (ms->version < 3)
    {
      /* Clear fd_nextsize and bk_nextsize fields.  */
      b = unsorted_chunks (&main_arena)->fd;
      while (b != unsorted_chunks (&main_arena))
        {
          if (!in_smallbin_range (chunksize (b)))
            {
              b->fd_nextsize = NULL;
              b->bk_nextsize = NULL;
            }
          b = b->fd;
        }
    }
  mp_.sbrk_base = ms->sbrk_base;
  main_arena.system_mem = ms->sbrked_mem_bytes;
  mp_.trim_threshold = ms->trim_threshold;
  mp_.top_pad = ms->top_pad;
  mp_.n_mmaps_max = ms->n_mmaps_max;
  mp_.mmap_threshold = ms->mmap_threshold;
  check_action = ms->check_action;
  main_arena.max_system_mem = ms->max_sbrked_mem;
  mp_.n_mmaps = ms->n_mmaps;
  mp_.max_n_mmaps = ms->max_n_mmaps;
  mp_.mmapped_mem = ms->mmapped_mem;
  mp_.max_mmapped_mem = ms->max_mmapped_mem;
  /* add version-dependent code here */
  if (ms->version >= 1)
    {
      /* Check whether it is safe to enable malloc checking, or whether
         it is necessary to disable it.  */
      if (ms->using_malloc_checking && !using_malloc_checking &&
          !disallow_malloc_check)
        __malloc_check_init ();
      else if (!ms->using_malloc_checking && using_malloc_checking)
        {
          __malloc_hook = NULL;
          __free_hook = NULL;
          __realloc_hook = NULL;
          __memalign_hook = NULL;
          using_malloc_checking = 0;
        }
    }
  if (ms->version >= 4)
    {
      mp_.arena_test = ms->arena_test;
      mp_.arena_max = ms->arena_max;
      narenas = ms->narenas;
    }
  check_malloc_state (&main_arena);

  (void) mutex_unlock (&main_arena.mutex);
  return 0;
}

/*
 * Local variables:
 * c-basic-offset: 2
 * End:
 */
