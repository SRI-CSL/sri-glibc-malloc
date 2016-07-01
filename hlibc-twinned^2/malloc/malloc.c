/* Malloc implementation for multiple threads without lock contention.
   Copyright (C) 1996-2015 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Wolfram Gloger <wg@malloc.de>
   and Doug Lea <dl@cs.oswego.edu>, 2001.

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

/*
  This is a version (aka ptmalloc2) of malloc/free/realloc written by
  Doug Lea and adapted to multiple threads/arenas by Wolfram Gloger.

  There have been substantial changes made after the integration into
  glibc in all parts of the code.  Do not look for much commonality
  with the ptmalloc2 version.

  * Version ptmalloc2-20011215
  based on:
  VERSION 2.7.0 Sun Mar 11 14:14:06 2001  Doug Lea  (dl at gee)

  * Quickstart

  In order to compile this implementation, a Makefile is provided with
  the ptmalloc2 distribution, which has pre-defined targets for some
  popular systems (e.g. "make posix" for Posix threads).  All that is
  typically required with regard to compiler flags is the selection of
  the thread package via defining one out of USE_PTHREADS, USE_THR or
  USE_SPROC.  Check the thread-m.h file for what effects this has.
  Many/most systems will additionally require USE_TSD_DATA_HACK to be
  defined, so this is the default for "make posix".

  * Why use this malloc?

  This is not the fastest, most space-conserving, most portable, or
  most tunable malloc ever written. However it is among the fastest
  while also being among the most space-conserving, portable and tunable.
  Consistent balance across these factors results in a good general-purpose
  allocator for malloc-intensive programs.

  The main properties of the algorithms are:
  * For large (>= 512 bytes) requests, it is a pure best-fit allocator,
  with ties normally decided via FIFO (i.e. least recently used).
  * For small (<= 64 bytes by default) requests, it is a caching
  allocator, that maintains pools of quickly recycled chunks.
  * In between, and for combinations of large and small requests, it does
  the best it can trying to meet both goals at once.
  * For very large requests (>= 128KB by default), it relies on system
  memory mapping facilities, if supported.

  For a longer but slightly out of date high-level description, see
  http://gee.cs.oswego.edu/dl/html/malloc.html

  You may already by default be using a C library containing a malloc
  that is  based on some version of this malloc (for example in
  linux). You might still want to use the one in this file in order to
  customize settings or to avoid overheads associated with library
  versions.

  * Contents, described in more detail in "description of public routines" below.

  Standard (ANSI/SVID/...)  functions:
  malloc(size_t n);
  calloc(size_t n_elements, size_t element_size);
  free(void* p);
  realloc(void* p, size_t n);
  memalign(size_t alignment, size_t n);
  valloc(size_t nfo()
  mallopt(int parameter_number, int parameter_value)

  Additional functions:
  independent_calloc(size_t n_elements, size_t size, void* chunks[]);
  independent_comalloc(size_t n_elements, size_t sizes[], void* chunks[]);
  pvalloc(size_t n);
  cfree(void* p);
  malloc_trim(size_t pad);
  malloc_usable_size(void* p);
  malloc_stats();

  * Vital statistics:

  Supported pointer representation:       4 or 8 bytes
  Supported size_t  representation:       4 or 8 bytes
  Note that size_t is allowed to be 4 bytes even if pointers are 8.
  You can adjust this by defining INTERNAL_SIZE_T

  Alignment:                              2 * sizeof(size_t) (default)
  (i.e., 8 byte alignment with 4byte size_t). This suffices for
  nearly all current machines and C compilers. However, you can
  define MALLOC_ALIGNMENT to be wider than this if necessary.

  Minimum overhead per allocated chunk:   4 or 8 bytes
  Each malloced chunk has a hidden word of overhead holding size
  and status information.

  Minimum allocated size: 4-byte ptrs:  16 bytes    (including 4 overhead)
  8-byte ptrs:  24/32 bytes (including, 4/8 overhead)

  When a chunk is freed, 12 (for 4byte ptrs) or 20 (for 8 byte
  ptrs but 4 byte size) or 24 (for 8/8) additional bytes are
  needed; 4 (8) for a trailing size field and 8 (16) bytes for
  free list pointers. Thus, the minimum allocatable size is
  16/24/32 bytes.

  Even a request for zero bytes (i.e., malloc(0)) returns a
  pointer to something of the minimum allocatable size.

  The maximum overhead wastage (i.e., number of extra bytes
  allocated than were requested in malloc) is less than or equal
  to the minimum size, except for requests >= mmap_threshold that
  are serviced via mmap(), where the worst case wastage is 2 *
  sizeof(size_t) bytes plus the remainder from a system page (the
  minimal mmap unit); typically 4096 or 8192 bytes.

  Maximum allocated size:  4-byte size_t: 2^32 minus about two pages
  8-byte size_t: 2^64 minus about two pages

  It is assumed that (possibly signed) size_t values suffice to
  represent chunk sizes. `Possibly signed' is due to the fact
  that `size_t' may be defined on a system as either a signed or
  an unsigned type. The ISO C standard says that it must be
  unsigned, but a few systems are known not to adhere to this.
  Additionally, even when size_t is unsigned, sbrk (which is by
  default used to obtain memory from system) accepts signed
  arguments, and may not be able to handle size_t-wide arguments
  with negative sign bit.  Generally, values that would
  appear as negative after accounting for overhead and alignment
  are supported only via mmap(), which does not have this
  limitation.

  Requests for sizes outside the allowed range will perform an optional
  failure action and then return null. (Requests may also
  also fail because a system is out of memory.)

  Thread-safety: thread-safe

  Compliance: I believe it is compliant with the 1997 Single Unix Specification
  Also SVID/XPG, ANSI C, and probably others as well.

  * Synopsis of compile-time options:

  People have reported using previous versions of this malloc on all
  versions of Unix, sometimes by tweaking some of the defines
  below. It has been tested most extensively on Solaris and Linux.
  People also report using it in stand-alone embedded systems.

  The implementation is in straight, hand-tuned ANSI C.  It is not
  at all modular. (Sorry!)  It uses a lot of macros.  To be at all
  usable, this code should be compiled using an optimizing compiler
  (for example gcc -O3) that can simplify expressions and control
  paths. (FAQ: some macros import variables as arguments rather than
  declare locals because people reported that some debuggers
  otherwise get confused.)

  OPTION                     DEFAULT VALUE

  Compilation Environment options:

  HAVE_MREMAP                0

  Changing default word sizes:

  INTERNAL_SIZE_T            size_t
  MALLOC_ALIGNMENT           MAX (2 * sizeof(INTERNAL_SIZE_T),
  __alignof__ (long double))

  Configuration and functionality options:

  USE_PUBLIC_MALLOC_WRAPPERS NOT defined
  USE_MALLOC_LOCK            NOT defined
  MALLOC_DEBUG               NOT defined
  REALLOC_ZERO_BYTES_FREES   1
  TRIM_FASTBINS              0

  Options for customizing MORECORE:

  MORECORE                   sbrk
  MORECORE_FAILURE           -1
  MORECORE_CONTIGUOUS        1
  MORECORE_CANNOT_TRIM       NOT defined
  MORECORE_CLEARS            1
  MMAP_AS_MORECORE_SIZE      (1024 * 1024)

  Tuning options that are also dynamically changeable via mallopt:

  DEFAULT_MXFAST             64 (for 32bit), 128 (for 64bit)
  DEFAULT_TRIM_THRESHOLD     128 * 1024
  DEFAULT_TOP_PAD            0
  DEFAULT_MMAP_THRESHOLD     128 * 1024
  DEFAULT_MMAP_MAX           65536

  There are several other #defined constants and macros that you
  probably don't want to touch unless you are extending or adapting malloc.  */

/*
  void* is the pointer type that malloc should say it returns
*/

#ifndef void
#define void      void
#endif /*void*/

#include <stddef.h>   /* for size_t */
#include <stdlib.h>   /* for getenv(), abort() */
#include <unistd.h>   /* for __libc_enable_secure */
#include <stdbool.h>  /* for bool, true and false, D'oh */

#include <malloc-machine.h>
#include <malloc-sysdep.h>

#include <atomic.h>
#include <_itoa.h>
#include <bits/wordsize.h>
#include <sys/sysinfo.h>

#include <ldsodefs.h>

#include <unistd.h>
#include <stdio.h>    /* needed for malloc_stats */
#include <errno.h>

#include <shlib-compat.h>

/* For uintptr_t.  */
#include <stdint.h>

/* For va_arg, va_start, va_end.  */
#include <stdarg.h>

/* For MIN, MAX, powerof2.  */
#include <sys/param.h>

/* For ALIGN_UP et. al.  */
#include <libc-internal.h>

/* 
   SRI: we need to use assert as well, so we pull it out into it's own
   header file
*/
#include "gassert.h"

/* SRI's  metatdata header */
#include "metadata.h"
#include "lookup.h"

/*
  Debugging:

  Because freed chunks may be overwritten with bookkeeping fields, this
  malloc will often die when freed memory is overwritten by user
  programs.  This can be very effective (albeit in an annoying way)
  in helping track down dangling pointers.

  If you compile with -DMALLOC_DEBUG, a number of assertion checks are
  enabled that will catch more memory errors. You probably won't be
  able to make much sense of the actual assertion errors, but they
  should help you locate incorrectly overwritten memory.  The checking
  is fairly extensive, and will slow down execution
  noticeably. Calling malloc_stats or mallinfo with MALLOC_DEBUG set
  will attempt to check every non-mmapped allocated and free chunk in
  the course of computing the summmaries. (By nature, mmapped regions
  cannot be checked very much automatically.)

  Setting MALLOC_DEBUG may also be helpful if you are trying to modify
  this code. The assertions in the check routines spell out in more
  detail the assumptions and invariants underlying the algorithms.

  Setting MALLOC_DEBUG does NOT provide an automated mechanism for
  checking that all accesses to malloced memory stay within their
  bounds. However, there are several add-ons and adaptations of this
  or other mallocs available that do this.
*/

#ifndef MALLOC_DEBUG
#define MALLOC_DEBUG 0
#endif

/*
  INTERNAL_SIZE_T is the word-size used for internal bookkeeping
  of chunk sizes.

  The default version is the same as size_t.

  While not strictly necessary, it is best to define this as an
  unsigned type, even if size_t is a signed type. This may avoid some
  artificial size limitations on some systems.

  On a 64-bit machine, you may be able to reduce malloc overhead by
  defining INTERNAL_SIZE_T to be a 32 bit `unsigned int' at the
  expense of not being able to handle more than 2^32 of malloced
  space. If this limitation is acceptable, you are encouraged to set
  this unless you are on a platform requiring 16byte alignments. In
  this case the alignment requirements turn out to negate any
  potential advantages of decreasing size_t word size.

  Implementors: Beware of the possible combinations of:
  - INTERNAL_SIZE_T might be signed or unsigned, might be 32 or 64 bits,
  and might be the same width as int or as long
  - size_t might have different width and signedness as INTERNAL_SIZE_T
  - int and long might be 32 or 64 bits, and might be the same width
  To deal with this, most comparisons and difference computations
  among INTERNAL_SIZE_Ts should cast them to unsigned long, being
  aware of the fact that casting an unsigned int to a wider long does
  not sign-extend. (This also makes checking for negative numbers
  awkward.) Some of these casts result in harmless compiler warnings
  on some systems.
*/

#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

/* The corresponding word size */
#define SIZE_SZ                (sizeof(INTERNAL_SIZE_T))


/*
  MALLOC_ALIGNMENT is the minimum alignment for malloc'ed chunks.
  It must be a power of two at least 2 * SIZE_SZ, even on machines
  for which smaller alignments would suffice. It may be defined as
  larger than this though. Note however that code and data structures
  are optimized for the case of 8-byte alignment.
*/


#ifndef MALLOC_ALIGNMENT
# if !SHLIB_COMPAT (libc, GLIBC_2_0, GLIBC_2_16)
/* This is the correct definition when there is no past ABI to constrain it.

   Among configurations with a past ABI constraint, it differs from
   2*SIZE_SZ only on powerpc32.  For the time being, changing this is
   causing more compatibility problems due to malloc_get_state and
   malloc_set_state than will returning blocks not adequately aligned for
   long double objects under -mlong-double-128.  */

#  define MALLOC_ALIGNMENT       (2 *SIZE_SZ < __alignof__ (long double) \
                                  ? __alignof__ (long double) : 2 *SIZE_SZ)
# else
#  define MALLOC_ALIGNMENT       (2 *SIZE_SZ)
# endif
#endif

/* The corresponding bit mask value */
#define MALLOC_ALIGN_MASK      (MALLOC_ALIGNMENT - 1)



/*
  REALLOC_ZERO_BYTES_FREES should be set if a call to
  realloc with zero bytes should be the same as a call to free.
  This is required by the C standard. Otherwise, since this malloc
  returns a unique pointer for malloc(0), so does realloc(p, 0).
*/

#ifndef REALLOC_ZERO_BYTES_FREES
#define REALLOC_ZERO_BYTES_FREES 1
#endif

/*
  TRIM_FASTBINS controls whether free() of a very small chunk can
  immediately lead to trimming. Setting to true (1) can reduce memory
  footprint, but will almost always slow down programs that use a lot
  of small chunks.

  Define this only if you are willing to give up some speed to more
  aggressively reduce system-level memory footprint when releasing
  memory in programs that use many small chunks.  You can get
  essentially the same effect by setting MXFAST to 0, but this can
  lead to even greater slowdowns in programs using many small chunks.
  TRIM_FASTBINS is an in-between compile-time option, that disables
  only those chunks bordering topmost memory from being placed in
  fastbins.
*/

#ifndef TRIM_FASTBINS
#define TRIM_FASTBINS  0
#endif


/* Definition for getting more memory from the OS.  */
#define MORECORE         (*__morecore)
#define MORECORE_FAILURE 0
void * __default_morecore (ptrdiff_t);
void *(*__morecore)(ptrdiff_t) = __default_morecore;


#include <string.h>

/*
  MORECORE-related declarations. By default, rely on sbrk
*/


/*
  MORECORE is the name of the routine to call to obtain more memory
  from the system.  See below for general guidance on writing
  alternative MORECORE functions, as well as a version for WIN32 and a
  sample version for pre-OSX macos.
*/

#ifndef MORECORE
#define MORECORE sbrk
#endif

/*
  MORECORE_FAILURE is the value returned upon failure of MORECORE
  as well as mmap. Since it cannot be an otherwise valid memory address,
  and must reflect values of standard sys calls, you probably ought not
  try to redefine it.
*/

#ifndef MORECORE_FAILURE
#define MORECORE_FAILURE (-1)
#endif

/*
  If MORECORE_CONTIGUOUS is true, take advantage of fact that
  consecutive calls to MORECORE with positive arguments always return
  contiguous increasing addresses.  This is true of unix sbrk.  Even
  if not defined, when regions happen to be contiguous, malloc will
  permit allocations spanning regions obtained from different
  calls. But defining this when applicable enables some stronger
  consistency checks and space efficiencies.
*/

#ifndef MORECORE_CONTIGUOUS
#define MORECORE_CONTIGUOUS 1
#endif

/*
  Define MORECORE_CANNOT_TRIM if your version of MORECORE
  cannot release space back to the system when given negative
  arguments. This is generally necessary only if you are using
  a hand-crafted MORECORE function that cannot handle negative arguments.
*/

/* #define MORECORE_CANNOT_TRIM */

/*  MORECORE_CLEARS           (default 1)
    The degree to which the routine mapped to MORECORE zeroes out
    memory: never (0), only for newly allocated space (1) or always
    (2).  The distinction between (1) and (2) is necessary because on
    some systems, if the application first decrements and then
    increments the break value, the contents of the reallocated space
    are unspecified.
*/

#ifndef MORECORE_CLEARS
# define MORECORE_CLEARS 1
#endif


/*
  MMAP_AS_MORECORE_SIZE is the minimum mmap size argument to use if
  sbrk fails, and mmap is used as a backup.  The value must be a
  multiple of page size.  This backup strategy generally applies only
  when systems have "holes" in address space, so sbrk cannot perform
  contiguous expansion, but there is still space available on system.
  On systems for which this is known to be useful (i.e. most linux
  kernels), this occurs only when programs allocate huge amounts of
  memory.  Between this, and the fact that mmap regions tend to be
  limited, the size should be large, to avoid too many mmap calls and
  thus avoid running out of kernel resources.  */

#ifndef MMAP_AS_MORECORE_SIZE
#define MMAP_AS_MORECORE_SIZE (1024 * 1024)
#endif

/*
  Define HAVE_MREMAP to make realloc() use mremap() to re-allocate
  large blocks.
*/

#ifndef HAVE_MREMAP
#define HAVE_MREMAP 0
#endif


/*
  This version of malloc supports the standard SVID/XPG mallinfo
  routine that returns a struct containing usage properties and
  statistics. It should work on any SVID/XPG compliant system that has
  a /usr/include/malloc.h defining struct mallinfo. (If you'd like to
  install such a thing yourself, cut out the preliminary declarations
  as described above and below and save them in a malloc.h file. But
  there's no compelling reason to bother to do this.)

  The main declaration needed is the mallinfo struct that is returned
  (by-copy) by mallinfo().  The SVID/XPG malloinfo struct contains a
  bunch of fields that are not even meaningful in this version of
  malloc.  These fields are are instead filled by mallinfo() with
  other numbers that might be of interest.
*/


/* ---------- description of public routines ------------ */

/*
  malloc(size_t n)
  Returns a pointer to a newly allocated chunk of at least n bytes, or null
  if no space is available. Additionally, on failure, errno is
  set to ENOMEM on ANSI C systems.

  If n is zero, malloc returns a minumum-sized chunk. (The minimum
  size is 16 bytes on most 32bit systems, and 24 or 32 bytes on 64bit
  systems.)  On most systems, size_t is an unsigned type, so calls
  with negative arguments are interpreted as requests for huge amounts
  of space, which will often fail. The maximum supported value of n
  differs across systems, but is in all cases less than the maximum
  representable value of a size_t.
*/
void*  __libc_malloc(size_t);
libc_hidden_proto (__libc_malloc)

/*
  free(void* p)
  Releases the chunk of memory pointed to by p, that had been previously
  allocated using malloc or a related routine such as realloc.
  It has no effect if p is null. It can have arbitrary (i.e., bad!)
  effects if p has already been freed.

  Unless disabled (using mallopt), freeing very large spaces will
  when possible, automatically trigger operations that give
  back unused memory to the system, thus reducing program footprint.
*/
void     __libc_free(void*);
libc_hidden_proto (__libc_free)

/*
  calloc(size_t n_elements, size_t element_size);
  Returns a pointer to n_elements * element_size bytes, with all locations
  set to zero.
*/
void*  __libc_calloc(size_t, size_t);

/*
  realloc(void* p, size_t n)
  Returns a pointer to a chunk of size n that contains the same data
  as does chunk p up to the minimum of (n, p's size) bytes, or null
  if no space is available.

  The returned pointer may or may not be the same as p. The algorithm
  prefers extending p when possible, otherwise it employs the
  equivalent of a malloc-copy-free sequence.

  If p is null, realloc is equivalent to malloc.

  If space is not available, realloc returns null, errno is set (if on
  ANSI) and p is NOT freed.

  if n is for fewer bytes than already held by p, the newly unused
  space is lopped off and freed if possible.  Unless the #define
  REALLOC_ZERO_BYTES_FREES is set, realloc with a size argument of
  zero (re)allocates a minimum-sized chunk.

  Large chunks that were internally obtained via mmap will always
  be reallocated using malloc-copy-free sequences unless
  the system supports MREMAP (currently only linux).

  The old unix realloc convention of allowing the last-free'd chunk
  to be used as an argument to realloc is not supported.
*/
void*  __libc_realloc(void*, size_t);
libc_hidden_proto (__libc_realloc)

/*
  memalign(size_t alignment, size_t n);
  Returns a pointer to a newly allocated chunk of n bytes, aligned
  in accord with the alignment argument.

  The alignment argument should be a power of two. If the argument is
  not a power of two, the nearest greater power is used.
  8-byte alignment is guaranteed by normal malloc calls, so don't
  bother calling memalign with an argument of 8 or less.

  Overreliance on memalign is a sure way to fragment space.
*/
void*  __libc_memalign(size_t, size_t);
libc_hidden_proto (__libc_memalign)

/*
  valloc(size_t n);
  Equivalent to memalign(pagesize, n), where pagesize is the page
  size of the system. If the pagesize is unknown, 4096 is used.
*/
void*  __libc_valloc(size_t);



/*
  mallopt(int parameter_number, int parameter_value)
  Sets tunable parameters The format is to provide a
  (parameter-number, parameter-value) pair.  mallopt then sets the
  corresponding parameter to the argument value if it can (i.e., so
  long as the value is meaningful), and returns 1 if successful else
  0.  SVID/XPG/ANSI defines four standard param numbers for mallopt,
  normally defined in malloc.h.  Only one of these (M_MXFAST) is used
  in this malloc. The others (M_NLBLKS, M_GRAIN, M_KEEP) don't apply,
  so setting them has no effect. But this malloc also supports four
  other options in mallopt. See below for details.  Briefly, supported
  parameters are as follows (listed defaults are for "typical"
  configurations).

  Symbol            param #   default    allowed param values
  M_MXFAST          1         64         0-80  (0 disables fastbins)
  M_TRIM_THRESHOLD -1         128*1024   any   (-1U disables trimming)
  M_TOP_PAD        -2         0          any
  M_MMAP_THRESHOLD -3         128*1024   any   (or 0 if no MMAP support)
  M_MMAP_MAX       -4         65536      any   (0 disables use of mmap)
*/
int      __libc_mallopt(int, int);
libc_hidden_proto (__libc_mallopt)


/*
  mallinfo()
  Returns (by copy) a struct containing various summary statistics:

  arena:     current total non-mmapped bytes allocated from system
  ordblks:   the number of free chunks
  smblks:    the number of fastbin blocks (i.e., small chunks that
  have been freed but not use resused or consolidated)
  hblks:     current number of mmapped regions
  hblkhd:    total bytes held in mmapped regions
  usmblks:   the maximum total allocated space. This will be greater
  than current total if trimming has occurred.
  fsmblks:   total bytes held in fastbin blocks
  uordblks:  current total allocated space (normal or mmapped)
  fordblks:  total free space
  keepcost:  the maximum number of bytes that could ideally be released
  back to system via malloc_trim. ("ideally" means that
  it ignores page restrictions etc.)

  Because these fields are ints, but internal bookkeeping may
  be kept as longs, the reported values may wrap around zero and
  thus be inaccurate.
*/
struct mallinfo __libc_mallinfo(void);


/*
  pvalloc(size_t n);
  Equivalent to valloc(minimum-page-that-holds(n)), that is,
  round up n to nearest pagesize.
*/
void*  __libc_pvalloc(size_t);

/*
  malloc_trim(size_t pad);

  If possible, gives memory back to the system (via negative
  arguments to sbrk) if there is unused memory at the `high' end of
  the malloc pool. You can call this after freeing large blocks of
  memory to potentially reduce the system-level memory requirements
  of a program. However, it cannot guarantee to reduce memory. Under
  some allocation patterns, some large free blocks of memory will be
  locked between two used chunks, so they cannot be given back to
  the system.

  The `pad' argument to malloc_trim represents the amount of free
  trailing space to leave untrimmed. If this argument is zero,
  only the minimum amount of memory to maintain internal data
  structures will be left (one page or less). Non-zero arguments
  can be supplied to maintain enough trailing space to service
  future expected allocations without having to re-obtain memory
  from the system.

  Malloc_trim returns 1 if it actually released any memory, else 0.
  On systems that do not support "negative sbrks", it will always
  return 0.
*/
int      __malloc_trim(size_t);

/*
  malloc_usable_size(void* p);

  Returns the number of bytes you can actually use in
  an allocated chunk, which may be more than you requested (although
  often not) due to alignment and minimum size constraints.
  You can use this many bytes without worrying about
  overwriting other allocated objects. This is not a particularly great
  programming practice. malloc_usable_size can be more useful in
  debugging and assertions, for example:

  p = malloc(n);
  assert(malloc_usable_size(p) >= 256);

*/
size_t   __malloc_usable_size(void*);

/*
  malloc_stats();
  Prints on stderr the amount of space obtained from the system (both
  via sbrk and mmap), the maximum amount (which may be more than
  current if malloc_trim and/or munmap got called), and the current
  number of bytes allocated via malloc (or realloc, etc) but not yet
  freed. Note that this is the number of bytes allocated, not the
  number requested. It will be larger than the number requested
  because of alignment and bookkeeping overhead. Because it includes
  alignment wastage as being in use, this figure may be greater than
  zero even when no user-level chunks are allocated.

  The reported current and maximum system memory can be inaccurate if
  a program makes other calls to system memory allocation functions
  (normally sbrk) outside of malloc.

  malloc_stats prints only the most commonly interesting statistics.
  More information can be obtained by calling mallinfo.

*/
void     __malloc_stats(void);

/*
  malloc_get_state(void);

  Returns the state of all malloc variables in an opaque data
  structure.
*/
void*  __malloc_get_state(void);

/*
  malloc_set_state(void* state);

  Restore the state of all malloc variables from data obtained with
  malloc_get_state().
*/
int      __malloc_set_state(void*);

/*
  posix_memalign(void **memptr, size_t alignment, size_t size);

  POSIX wrapper like memalign(), checking for validity of size.
*/
int      __posix_memalign(void **, size_t, size_t);

/* mallopt tuning options */

/*
  M_MXFAST is the maximum request size used for "fastbins", special bins
  that hold returned chunks without consolidating their spaces. This
  enables future requests for chunks of the same size to be handled
  very quickly, but can increase fragmentation, and thus increase the
  overall memory footprint of a program.

  This malloc manages fastbins very conservatively yet still
  efficiently, so fragmentation is rarely a problem for values less
  than or equal to the default.  The maximum supported value of MXFAST
  is 80. You wouldn't want it any higher than this anyway.  Fastbins
  are designed especially for use with many small structs, objects or
  strings -- the default handles structs/objects/arrays with sizes up
  to 8 4byte fields, or small strings representing words, tokens,
  etc. Using fastbins for larger objects normally worsens
  fragmentation without improving speed.

  M_MXFAST is set in REQUEST size units. It is internally used in
  chunksize units, which adds padding and alignment.  You can reduce
  M_MXFAST to 0 to disable all use of fastbins.  This causes the malloc
  algorithm to be a closer approximation of fifo-best-fit in all cases,
  not just for larger requests, but will generally cause it to be
  slower.
*/


/* M_MXFAST is a standard SVID/XPG tuning option, usually listed in malloc.h */
#ifndef M_MXFAST
#define M_MXFAST            1
#endif

#ifndef DEFAULT_MXFAST
#define DEFAULT_MXFAST     (64 * SIZE_SZ / 4)
#endif


/*
  M_TRIM_THRESHOLD is the maximum amount of unused top-most memory
  to keep before releasing via malloc_trim in free().

  Automatic trimming is mainly useful in long-lived programs.
  Because trimming via sbrk can be slow on some systems, and can
  sometimes be wasteful (in cases where programs immediately
  afterward allocate more large chunks) the value should be high
  enough so that your overall system performance would improve by
  releasing this much memory.

  The trim threshold and the mmap control parameters (see below)
  can be traded off with one another. Trimming and mmapping are
  two different ways of releasing unused memory back to the
  system. Between these two, it is often possible to keep
  system-level demands of a long-lived program down to a bare
  minimum. For example, in one test suite of sessions measuring
  the XF86 X server on Linux, using a trim threshold of 128K and a
  mmap threshold of 192K led to near-minimal long term resource
  consumption.

  If you are using this malloc in a long-lived program, it should
  pay to experiment with these values.  As a rough guide, you
  might set to a value close to the average size of a process
  (program) running on your system.  Releasing this much memory
  would allow such a process to run in memory.  Generally, it's
  worth it to tune for trimming rather tham memory mapping when a
  program undergoes phases where several large chunks are
  allocated and released in ways that can reuse each other's
  storage, perhaps mixed with phases where there are no such
  chunks at all.  And in well-behaved long-lived programs,
  controlling release of large blocks via trimming versus mapping
  is usually faster.

  However, in most programs, these parameters serve mainly as
  protection against the system-level effects of carrying around
  massive amounts of unneeded memory. Since frequent calls to
  sbrk, mmap, and munmap otherwise degrade performance, the default
  parameters are set to relatively high values that serve only as
  safeguards.

  The trim value It must be greater than page size to have any useful
  effect.  To disable trimming completely, you can set to
  (unsigned long)(-1)

  Trim settings interact with fastbin (MXFAST) settings: Unless
  TRIM_FASTBINS is defined, automatic trimming never takes place upon
  freeing a chunk with size less than or equal to MXFAST. Trimming is
  instead delayed until subsequent freeing of larger chunks. However,
  you can still force an attempted trim by calling malloc_trim.

  Also, trimming is not generally possible in cases where
  the main arena is obtained via mmap.

  Note that the trick some people use of mallocing a huge space and
  then freeing it at program startup, in an attempt to reserve system
  memory, doesn't have the intended effect under automatic trimming,
  since that memory will immediately be returned to the system.
*/

#define M_TRIM_THRESHOLD       -1

#ifndef DEFAULT_TRIM_THRESHOLD
#define DEFAULT_TRIM_THRESHOLD (128 * 1024)
#endif

/*
  M_TOP_PAD is the amount of extra `padding' space to allocate or
  retain whenever sbrk is called. It is used in two ways internally:

  * When sbrk is called to extend the top of the arena to satisfy
  a new malloc request, this much padding is added to the sbrk
  request.

  * When malloc_trim is called automatically from free(),
  it is used as the `pad' argument.

  In both cases, the actual amount of padding is rounded
  so that the end of the arena is always a system page boundary.

  The main reason for using padding is to avoid calling sbrk so
  often. Having even a small pad greatly reduces the likelihood
  that nearly every malloc request during program start-up (or
  after trimming) will invoke sbrk, which needlessly wastes
  time.

  Automatic rounding-up to page-size units is normally sufficient
  to avoid measurable overhead, so the default is 0.  However, in
  systems where sbrk is relatively slow, it can pay to increase
  this value, at the expense of carrying around more memory than
  the program needs.
*/

#define M_TOP_PAD              -2

#ifndef DEFAULT_TOP_PAD
#define DEFAULT_TOP_PAD        (0)
#endif

/*
  MMAP_THRESHOLD_MAX and _MIN are the bounds on the dynamically
  adjusted MMAP_THRESHOLD.
*/

#ifndef DEFAULT_MMAP_THRESHOLD_MIN
#define DEFAULT_MMAP_THRESHOLD_MIN (128 * 1024)
#endif

#ifndef DEFAULT_MMAP_THRESHOLD_MAX
/* For 32-bit platforms we cannot increase the maximum mmap
   threshold much because it is also the minimum value for the
   maximum heap size and its alignment.  Going above 512k (i.e., 1M
   for new heaps) wastes too much address space.  */
# if __WORDSIZE == 32
#  define DEFAULT_MMAP_THRESHOLD_MAX (512 * 1024)
# else
#  define DEFAULT_MMAP_THRESHOLD_MAX (4 * 1024 * 1024 * sizeof(long))
# endif
#endif

/*
  M_MMAP_THRESHOLD is the request size threshold for using mmap()
  to service a request. Requests of at least this size that cannot
  be allocated using already-existing space will be serviced via mmap.
  (If enough normal freed space already exists it is used instead.)

  Using mmap segregates relatively large chunks of memory so that
  they can be individually obtained and released from the host
  system. A request serviced through mmap is never reused by any
  other request (at least not directly; the system may just so
  happen to remap successive requests to the same locations).

  Segregating space in this way has the benefits that:

  1. Mmapped space can ALWAYS be individually released back
  to the system, which helps keep the system level memory
  demands of a long-lived program low.
  2. Mapped memory can never become `locked' between
  other chunks, as can happen with normally allocated chunks, which
  means that even trimming via malloc_trim would not release them.
  3. On some systems with "holes" in address spaces, mmap can obtain
  memory that sbrk cannot.

  However, it has the disadvantages that:

  1. The space cannot be reclaimed, consolidated, and then
  used to service later requests, as happens with normal chunks.
  2. It can lead to more wastage because of mmap page alignment
  requirements
  3. It causes malloc performance to be more dependent on host
  system memory management support routines which may vary in
  implementation quality and may impose arbitrary
  limitations. Generally, servicing a request via normal
  malloc steps is faster than going through a system's mmap.

  The advantages of mmap nearly always outweigh disadvantages for
  "large" chunks, but the value of "large" varies across systems.  The
  default is an empirically derived value that works well in most
  systems.


  Update in 2006:
  The above was written in 2001. Since then the world has changed a lot.
  Memory got bigger. Applications got bigger. The virtual address space
  layout in 32 bit linux changed.

  In the new situation, brk() and mmap space is shared and there are no
  artificial limits on brk size imposed by the kernel. What is more,
  applications have started using transient allocations larger than the
  128Kb as was imagined in 2001.

  The price for mmap is also high now; each time glibc mmaps from the
  kernel, the kernel is forced to zero out the memory it gives to the
  application. Zeroing memory is expensive and eats a lot of cache and
  memory bandwidth. This has nothing to do with the efficiency of the
  virtual memory system, by doing mmap the kernel just has no choice but
  to zero.

  In 2001, the kernel had a maximum size for brk() which was about 800
  megabytes on 32 bit x86, at that point brk() would hit the first
  mmaped shared libaries and couldn't expand anymore. With current 2.6
  kernels, the VA space layout is different and brk() and mmap
  both can span the entire heap at will.

  Rather than using a static threshold for the brk/mmap tradeoff,
  we are now using a simple dynamic one. The goal is still to avoid
  fragmentation. The old goals we kept are
  1) try to get the long lived large allocations to use mmap()
  2) really large allocations should always use mmap()
  and we're adding now:
  3) transient allocations should use brk() to avoid forcing the kernel
  having to zero memory over and over again

  The implementation works with a sliding threshold, which is by default
  limited to go between 128Kb and 32Mb (64Mb for 64 bitmachines) and starts
  out at 128Kb as per the 2001 default.

  This allows us to satisfy requirement 1) under the assumption that long
  lived allocations are made early in the process' lifespan, before it has
  started doing dynamic allocations of the same size (which will
  increase the threshold).

  The upperbound on the threshold satisfies requirement 2)

  The threshold goes up in value when the application frees memory that was
  allocated with the mmap allocator. The idea is that once the application
  starts freeing memory of a certain size, it's highly probable that this is
  a size the application uses for transient allocations. This estimator
  is there to satisfy the new third requirement.

*/

#define M_MMAP_THRESHOLD      -3

#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD DEFAULT_MMAP_THRESHOLD_MIN
#endif

/*
  DEFAULT_MMAP_MAX is the maximum number of requests to simultaneously
  service using mmap. This parameter exists because
  some systems have a limited number of internal tables for
  use by mmap, and using more than a few of them may degrade
  performance.

  The default is set to a value that serves only as a safeguard.
  Setting to 0 disables use of mmap for servicing large requests.
*/

#define M_MMAP_MAX             -4

#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX       (65536)
#endif

#include <malloc.h>

#ifndef RETURN_ADDRESS
#define RETURN_ADDRESS(X_) (NULL)
#endif

/* On some platforms we can compile internal, not exported functions better.
   Let the environment provide a macro and define it to be empty if it
   is not available.  */
#ifndef internal_function
# define internal_function
#endif

/* Forward declarations.  */
struct malloc_chunk;
typedef struct malloc_chunk* mchunkptr;

/* Internal routines.  */
#define missing_metadata(AV,P) report_missing_metadata(AV, P, __FILE__, __LINE__)

static void report_missing_metadata(mstate av, mchunkptr p, const char* file, int lineno);

static chunkinfoptr new_chunkinfoptr(mstate av);
static bool replenish_metadata_cache(mstate av);

static chunkinfoptr lookup_chunk (mstate av, mchunkptr p);
static bool unregister_chunk (mstate av, mchunkptr p, int tag);
static chunkinfoptr register_chunk(mstate av, mchunkptr p, bool is_mmapped, int tag);

static chunkinfoptr split_chunk(mstate av, chunkinfoptr _md_victim, mchunkptr victim, INTERNAL_SIZE_T victim_size, INTERNAL_SIZE_T desiderata);

static mchunkptr chunkinfo2chunk(chunkinfoptr _md_victim);
static void*  chunkinfo2mem(chunkinfoptr _md_victim);


static chunkinfoptr  _int_malloc(mstate, size_t);
static void   _int_free(mstate, chunkinfoptr, mchunkptr, bool);
static chunkinfoptr  _int_realloc(mstate, chunkinfoptr, INTERNAL_SIZE_T,
                                  INTERNAL_SIZE_T);
static chunkinfoptr _int_memalign(mstate, size_t, size_t);
static void*  _mid_memalign(size_t, size_t, void *);

static void malloc_printerr(int action, const char *str, void *ptr, mstate av);

static void* internal_function mem2mem_check(chunkinfoptr _md_p, mchunkptr p, void *mem, size_t req_sz);
static int internal_function top_check(void);
static void internal_function munmap_chunk(chunkinfoptr _md_p);
#if HAVE_MREMAP
static mchunkptr internal_function mremap_chunk(mstate av, chunkinfoptr p, size_t new_size);
#endif

static void*   malloc_check(size_t sz, const void *caller);
static void      free_check(void* mem, const void *caller);
static void*   realloc_check(void* oldmem, size_t bytes,
                             const void *caller);
static void*   memalign_check(size_t alignment, size_t bytes,
                              const void *caller);
#ifndef NO_THREADS
static void*   malloc_atfork(size_t sz, const void *caller);
static void      free_atfork(void* mem, const void *caller);
#endif

/* ------------------ MMAP support ------------------  */


#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_NORESERVE
# define MAP_NORESERVE 0
#endif


/* 
   SRI: we plan to see if we can inline most of the #defines as part of a code cleanup.
   The sys_ prefix is to make it easy to search for.
 */
static inline void *sys_MMAP(void *addr, size_t length, int prot, int flags)
{
  return __mmap(addr, length, prot, flags|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
}

/*
  -----------------------  Chunk representations -----------------------
*/


/*
  The metadata left after culling. Because of alignment issues
  this header should either be 0 (hard) or 16 bytes long.
  Hence the reason we leave the unused __canary__ slot there.
*/

struct malloc_chunk {
  INTERNAL_SIZE_T      __canary__;   /* where prev_size use to live.  */
  INTERNAL_SIZE_T      arena_index;  /* index of arena:  0: mmapped 1: Main Arena  N+1: Nth arena */
};

#define HEADER_SIZE  sizeof(struct malloc_chunk)


/*
  ---------- Size and alignment checks and conversions ----------
*/

/* conversion from malloc headers to user pointers, and back */

static inline void *chunk2mem(void* p)
{
  return ((void*)((char*)p + HEADER_SIZE));
} 

static inline mchunkptr mem2chunk(void* mem)
{
  return ((mchunkptr)((char*)mem - HEADER_SIZE));
}

/* The smallest possible chunk */
#define MIN_CHUNK_SIZE       2 * sizeof(struct malloc_chunk)

/* The smallest size we can malloc is an aligned minimal chunk */

#define MINSIZE                                                         \
  (unsigned long)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

/* Check if m has acceptable alignment */

static inline bool aligned_OK(unsigned long m)
{
  return (((unsigned long)m & MALLOC_ALIGN_MASK) == 0);
}

static inline int misaligned_chunk(void* p)
{
  return ((uintptr_t)(MALLOC_ALIGNMENT == 2 * SIZE_SZ ? p : chunk2mem(p)) & MALLOC_ALIGN_MASK);
}

/*
  Check if a request is so large that it would wrap around zero when
  padded and aligned. To simplify some other code, the bound is made
  low enough so that adding MINSIZE will also not wrap around zero.
*/

static inline bool REQUEST_OUT_OF_RANGE(size_t req)
{
  return (unsigned long)req >=   (unsigned long) (INTERNAL_SIZE_T) (-2 * MINSIZE);
}

/* 
   pad request bytes into a usable size -- internal version 
   (SRI: removed sharing of prev_size)
*/

#define request2size(req)                                       \
  (((req) + HEADER_SIZE + MALLOC_ALIGN_MASK < MINSIZE)  ?           \
   MINSIZE :                                                    \
   ((req) + HEADER_SIZE + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK)

/*  Same, except also perform argument check */

static inline bool checked_request2size(size_t req, size_t *sz)
{
  assert(sz != NULL);
  if (REQUEST_OUT_OF_RANGE (req)) {
    __set_errno (ENOMEM);       
    return false;                       
  }                             
  *sz = request2size (req);
  return true;
}



/*
  --------------- Physical chunk operations ---------------
*/


/* size field is or'ed with PREV_INUSE when previous adjacent chunk in use */
#define PREV_INUSE 0x1

/* extract inuse bit of previous chunk */
static inline bool prev_inuse(chunkinfoptr _md_p, mchunkptr p)
{
  bool retval = false;

  assert(_md_p != NULL);
  assert(chunkinfo2chunk(_md_p) == p);

  if(_md_p != NULL){
    retval = ((_md_p->size & PREV_INUSE) == PREV_INUSE);
  }

  return retval;
}


/* arena index constants. The index is stored in the 
 * arena_index field of the malloc_chunk. It indicates
 * ownership of the chunk.
 *
 *  0    means the chunk is mmapped (currently the main_arena has jurisdiction).
 *  1    means the chunk belongs to the main arena's heap
 *  N+1  means the chunk belongs to the Nth arena.
 *
 */
#define MMAPPED_ARENA_INDEX  0x0
#define MAIN_ARENA_INDEX     0x1
#define NON_MAIN_ARENA_INDEX 0x2

/*
 *  SRI: returns true if the arena_index in p makes sense, it doesn't
 * mean that p actually came from that arena, just that is claims to
 * have.  It returns false otherwise
*/
static bool _arena_is_sane(mchunkptr p, const char* file, int lineno);

#define arena_is_sane(P) _arena_is_sane(P, __FILE__, __LINE__)

INTERNAL_SIZE_T get_arena_index(mchunkptr p)
{
  assert(p != NULL);
  return p->arena_index;
}
  
static int arena_index(mstate av);

static inline void set_arena_index(mstate av, mchunkptr p, INTERNAL_SIZE_T index)
{
  assert(p != NULL);
  p->arena_index = index;
}


/* check for mmap()'ed chunk */
static inline bool chunk_is_mmapped(mchunkptr p)
{
  return  p->arena_index == MMAPPED_ARENA_INDEX;
}

/* check for chunk from non-main arena */
static inline bool chunk_non_main_arena(mchunkptr p)
{
 return  p->arena_index >= NON_MAIN_ARENA_INDEX;
}


/*

  Bits to mask off when extracting size

*/
#define SIZE_BITS (PREV_INUSE)

/* Ptr to next physical malloc_chunk. */
static inline mchunkptr next_chunk(chunkinfoptr _md_p, mchunkptr p)
{
  assert(_md_p != NULL);
  assert(chunkinfo2chunk(_md_p) == p);
  return ((mchunkptr)( ((char*)p) + (_md_p->size & ~SIZE_BITS) ));
}

/* Ptr to previous physical malloc_chunk */
static inline mchunkptr prev_chunk(chunkinfoptr _md_p, mchunkptr p)
{
  assert(_md_p != NULL);
  assert(chunkinfo2chunk(_md_p) == p);
  return ((mchunkptr)( ((char*)p) - (_md_p->prev_size) ));
}

/* Treat space at ptr + offset as a chunk */
static inline mchunkptr chunk_at_offset(void* p, size_t s)
{
  return ((mchunkptr) (((char *) p) + s));
}

/* extract p's inuse bit */
static inline bool inuse(mstate av, chunkinfoptr _md_p, mchunkptr p)
{
  chunkinfoptr _md_next;
  mchunkptr next;
  assert(_md_p != NULL);
  assert(chunkinfo2chunk(_md_p) == p);
  next =  next_chunk(_md_p, p);
  _md_next = lookup_chunk(av, next);
  return prev_inuse(_md_next, next) ==  PREV_INUSE;
}

static inline INTERNAL_SIZE_T chunksize(chunkinfoptr ci);
  
/* check/set/clear inuse bits in known places */
static inline int inuse_bit_at_offset(mstate av, chunkinfoptr _md_p, mchunkptr p, size_t s)
{
  chunkinfoptr _md_next;
  mchunkptr next;

  assert(_md_p != NULL);
  assert(chunkinfo2chunk(_md_p) == p);

  assert( (s == 0) || (s == chunksize(_md_p))); 

  next =  chunk_at_offset(p, s);
  _md_next = lookup_chunk(av, next);

  if(_md_next == NULL){
    missing_metadata(av, next);
  } 

  return prev_inuse(_md_next, next);
}

static inline void set_inuse_bit_at_offset(mstate av, chunkinfoptr _md_p,  mchunkptr p, size_t s)
{
  chunkinfoptr _md_next_chunk;
  mchunkptr next_chunk;

  assert( (s == 0) || (s == chunksize(_md_p))); 

  next_chunk = (mchunkptr)(((char*)p) + s);
  _md_next_chunk = lookup_chunk(av, next_chunk);

  if (_md_next_chunk != NULL) {
    _md_next_chunk->size |= PREV_INUSE;
  } else {
    fprintf(stderr, "Setting inuse bit of %p to be %zu. _md is missing\n", 
            next_chunk,  s);
    abort();
  }
}

static inline void clear_inuse_bit_at_offset(mstate av, chunkinfoptr _md_p, mchunkptr p, size_t s)
{
  chunkinfoptr _md_next_chunk;
  mchunkptr next_chunk;

  assert( (s == 0) || (s == chunksize(_md_p))); 

  next_chunk = (mchunkptr)(((char*)p) + s);
  _md_next_chunk = lookup_chunk(av, next_chunk);

  if (_md_next_chunk != NULL) {
    _md_next_chunk->size &= ~PREV_INUSE;
  } else {
    fprintf(stderr, "Clearing inuse bit of %p to be %zu. _md is missing\n", 
            next_chunk,  s);
    abort();
  }
}

/* Set size/use field */
static inline void set_head(chunkinfoptr _md_p, size_t s)
{
  assert(_md_p != NULL);
  if(_md_p != NULL){
    _md_p->size = s;
  }
}

/* Set size at head, without disturbing its use bit */
static inline void set_head_size(chunkinfoptr _md_p, size_t s)
{
  set_head(_md_p, (_md_p->size & SIZE_BITS) | s);
}



/* Set size at footer (only when chunk is not in use) */
static inline void set_foot(mstate av, chunkinfoptr _md_p, mchunkptr p, size_t s)
{
  chunkinfoptr _md_next_chunk;
  mchunkptr next_chunk;

  next_chunk = (mchunkptr)((char*)p + s);
  _md_next_chunk = lookup_chunk(av, next_chunk);

  if (_md_next_chunk != NULL) {
    _md_next_chunk->prev_size =  s;
  } else {
    fprintf(stderr, "Setting prev_size of %p to be %zu. _md is missing\n", 
            next_chunk,  s);
    abort();
  }
}

/*
  -------------------- Internal data structures --------------------

  All internal state is held in an instance of malloc_state defined
  below. There are no other static variables, except in two optional
  cases:
  * If USE_MALLOC_LOCK is defined, the mALLOC_MUTEx declared above.
  * If mmap doesn't support MAP_ANONYMOUS, a dummy file descriptor
  for mmap.

  Beware of lots of tricks that minimize the total bookkeeping space
  requirements. The result is a little over 1K bytes (for 4byte
  pointers and size_t.)
*/

/*
  Bins

  An array of bin headers for free chunks. Each bin is doubly
  linked.  The bins are approximately proportionally (log) spaced.
  There are a lot of these bins (128). This may look excessive, but
  works very well in practice.  Most bins hold sizes that are
  unusual as malloc request sizes, but are more usual for fragments
  and consolidated sets of chunks, which is what these bins hold, so
  they can be found quickly.  All procedures maintain the invariant
  that no consolidated chunk physically borders another one, so each
  chunk in a list is known to be preceeded and followed by either
  inuse chunks or the ends of memory.

  Chunks in bins are kept in size order, with ties going to the
  approximately least recently used chunk. Ordering isn't needed
  for the small bins, which all contain the same-sized chunks, but
  facilitates best-fit allocation for larger chunks. These lists
  are just sequential. Keeping them in order almost never requires
  enough traversal to warrant using fancier ordered data
  structures.

  Chunks of the same size are linked with the most
  recently freed at the front, and allocations are taken from the
  back.  This results in LRU (FIFO) allocation order, which tends
  to give each chunk an equal opportunity to be consolidated with
  adjacent freed chunks, resulting in larger free chunks and less
  fragmentation.

  To simplify use in double-linked lists, each bin header acts
  as a malloc_chunk. This avoids special-casing for headers.
  But to conserve space and improve locality, we allocate
  only the fd/bk pointers of bins, and then use repositioning tricks
  to treat these as the fields of a malloc_chunk*.
*/

typedef struct chunkinfo *mbinptr;

/* addressing -- note that bin_at(0) does not exist */
static inline mbinptr bin_at(mstate av, int i);

/* analog of ++bin */
static inline mbinptr next_bin(mbinptr b);

/* Reminders about list directionality within bins */
#define first(b)     ((b)->fd)
#define last(b)      ((b)->bk)

/* Take a chunk off a bin list */
static inline void bin_unlink(mstate av, chunkinfoptr p, chunkinfoptr *bkp, chunkinfoptr *fdp);


/*
  Indexing

  Bins for sizes < 512 bytes contain chunks of all the same size, spaced
  8 bytes apart. Larger bins are approximately logarithmically spaced:

  64 bins of size       8
  32 bins of size      64
  16 bins of size     512
  8 bins of size    4096
  4 bins of size   32768
  2 bins of size  262144
  1 bin  of size what's left

  There is actually a little bit of slop in the numbers in bin_index
  for the sake of speed. This makes no difference elsewhere.

  The bins top out around 1MB because we expect to service large
  requests via mmap.

  Bin 0 does not exist.  Bin 1 is the unordered list; if that would be
  a valid chunk size the small bins are bumped up one.
*/

#define NBINS               128
#define NSMALLBINS          64
#define SMALLBIN_WIDTH      MALLOC_ALIGNMENT
#define SMALLBIN_CORRECTION (MALLOC_ALIGNMENT > 2 * SIZE_SZ)
#define MIN_LARGE_SIZE      ((NSMALLBINS - SMALLBIN_CORRECTION) * SMALLBIN_WIDTH)

static inline bool in_smallbin_range(INTERNAL_SIZE_T sz);
static inline unsigned int smallbin_index(INTERNAL_SIZE_T sz);
static inline unsigned int largebin_index_32(INTERNAL_SIZE_T sz);
static inline unsigned int largebin_index_32_big(INTERNAL_SIZE_T sz);
static inline unsigned int largebin_index_64(INTERNAL_SIZE_T sz);
static inline unsigned int largebin_index(INTERNAL_SIZE_T sz);
static inline unsigned int bin_index(INTERNAL_SIZE_T sz);


/*
  Unsorted chunks

  All remainders from chunk splits, as well as all returned chunks,
  are first placed in the "unsorted" bin. They are then placed
  in regular bins after malloc gives them ONE chance to be used before
  binning. So, basically, the unsorted_chunks list acts as a queue,
  with chunks being placed on it in free (and malloc_consolidate),
  and taken off (to be either used or placed in bins) in malloc.

*/

/* The otherwise unindexable 1-bin is used to hold unsorted chunks. */
#define unsorted_chunks(M)          (bin_at (M, 1))

/*
  Top

  The top-most available chunk (i.e., the one bordering the end of
  available memory) is treated specially. It is never included in
  any bin, is used only if no other chunk is available, and is
  released back to the system if it is very large (see
  M_TRIM_THRESHOLD).  Because top initially
  points to its own bin with initial zero size, thus forcing
  extension on the first malloc request, we avoid having any special
  code in malloc to check whether it even exists yet. But we still
  need to do so when getting memory from system, so we make a default
  initial_top as part of the malloc_state.
*/

/*
  Binmap

  To help compensate for the large number of bins, a one-level index
  structure is used for bin-by-bin searching.  `binmap' is a
  bitvector recording whether bins are definitely empty so they can
  be skipped over during during traversals.  The bits are NOT always
  cleared as soon as bins are empty, but instead only
  when they are noticed to be empty during traversal in malloc.
*/

/* Conservatively use 32 bits per map word, even if on 64bit system */
#define BINMAPSHIFT      5
#define BITSPERMAP       (1U << BINMAPSHIFT)
#define BINMAPSIZE       (NBINS / BITSPERMAP)

static inline unsigned int idx2block(unsigned  int i)
{
  return i >> BINMAPSHIFT;
}

static inline unsigned  int idx2bit(unsigned int i)
{
  return ((1U << (i & ((1U << BINMAPSHIFT) - 1))));
}

static inline void mark_bin(mstate av, int i);
static inline void unmark_bin(mstate av, int i);
static inline unsigned int get_binmap(mstate av, int i);


/*
  Fastbins

  An array of lists holding recently freed small chunks.  Fastbins
  are not doubly linked.  It is faster to single-link them, and
  since chunks are never removed from the middles of these lists,
  double linking is not necessary. Also, unlike regular bins, they
  are not even processed in FIFO order (they use faster LIFO) since
  ordering doesn't much matter in the transient contexts in which
  fastbins are normally used.

  Chunks in fastbins keep their inuse bit set, so they cannot
  be consolidated with other free chunks. malloc_consolidate
  releases all chunks in fastbins and consolidates them with
  other free chunks.
*/

typedef struct chunkinfo *mfastbinptr;

#define fastbin(ar_ptr, idx)                    \
  ((ar_ptr)->fastbinsY[idx])

/* offset 2 to use otherwise unindexable first 2 bins */
#define fastbin_index(sz)                                       \
  ((((unsigned int) (sz)) >> (SIZE_SZ == 8 ? 4 : 3)) - 2)

/* The maximum fastbin request size we support */
#define MAX_FAST_SIZE     (80 * SIZE_SZ / 4)

#define NFASTBINS  (fastbin_index (request2size (MAX_FAST_SIZE)) + 1)

/*
  FASTBIN_CONSOLIDATION_THRESHOLD is the size of a chunk in free()
  that triggers automatic consolidation of possibly-surrounding
  fastbin chunks. This is a heuristic, so the exact value should not
  matter too much. It is defined at half the default trim threshold as a
  compromise heuristic to only attempt consolidation if it is likely
  to lead to trimming. However, it is not dynamically tunable, since
  consolidation reduces fragmentation surrounding large chunks even
  if trimming is not used.
*/

#define FASTBIN_CONSOLIDATION_THRESHOLD  (65536UL)

/*
  Since the lowest 2 bits in max_fast don't matter in size comparisons,
  they are used as flags.
*/

/*
  FASTCHUNKS_BIT held in max_fast indicates that there are probably
  some fastbin chunks. It is set true on entering a chunk into any
  fastbin, and cleared only in malloc_consolidate.

  The truth value is inverted so that have_fastchunks will be true
  upon startup (since statics are zero-filled), simplifying
  initialization checks.
*/

#define FASTCHUNKS_BIT        (1U)

/* SRI: definitions move to after mstate definition */
static inline bool have_fastchunks(mstate av);
static inline void clear_fastchunks(mstate av);
static inline void set_fastchunks(mstate av);


/*
  NONCONTIGUOUS_BIT indicates that MORECORE does not return contiguous
  regions.  Otherwise, contiguity is exploited in merging together,
  when possible, results from consecutive MORECORE calls.

  The initial value comes from MORECORE_CONTIGUOUS, but is
  changed dynamically if mmap is ever used as an sbrk substitute.
*/

#define NONCONTIGUOUS_BIT     (2U)

/* SRI: definitions move to after mstate definition */
static inline bool contiguous(mstate av);
static inline bool noncontiguous(mstate av);
static inline void set_noncontiguous(mstate av);
static inline void set_contiguous(mstate av);

/* ARENA_CORRUPTION_BIT is set if a memory corruption was detected on the
   arena.  Such an arena is no longer used to allocate chunks.  Chunks
   allocated in that arena before detecting corruption are not freed.  */

#define ARENA_CORRUPTION_BIT (4U)

/* SRI: definitions move to after mstate definition */
static inline bool arena_is_corrupt(mstate av);
static inline void set_arena_corrupt(mstate av);

/*
  Set value of max_fast.
  Use impossibly small value if 0.
  Precondition: there are no existing fastbin chunks.
  Setting the value clears fastchunk bit but preserves noncontiguous bit.
*/

static void set_max_fast(INTERNAL_SIZE_T sz);
static INTERNAL_SIZE_T get_max_fast(void);


/*
  ----------- Internal state representation and initialization -----------
*/

/*
  Maximum number of metadata needed to service a request to the
  malloc api
*/
#define METADATA_CACHE_SIZE 8

struct malloc_state
{
  /* Serialize access.  */
  mutex_t mutex;

  /* Flags (formerly in max_fast).  */
  int flags;

  /* Fastbins */
  mfastbinptr fastbinsY[NFASTBINS];

  /* Metadata of the base of the topmost chunk -- not otherwise kept in a bin */
  chunkinfoptr _md_top;

  /* temporary value of initial top while we are in transition. */
  struct malloc_chunk  initial_top;        

  /* Metadata of the remainder from the most recent split of a small request */
  chunkinfoptr last_remainder;

  /* Normal bins packed as described above */
  struct chunkinfo bins[NBINS];

  /* Bitmap of bins */
  unsigned int binmap[BINMAPSIZE];

  /* Linked list */
  struct malloc_state *next;

  /* Linked list for free arenas.  Access to this field is serialized
     by list_lock in arena.c.  */
  struct malloc_state *next_free;

  /* Number of threads attached to this arena.  0 if the arena is on
     the free list.  Access to this field is serialized by list_lock
     in arena.c.  */
  INTERNAL_SIZE_T attached_threads;

  /* Memory allocated from the system in this arena.  */
  INTERNAL_SIZE_T system_mem;
  INTERNAL_SIZE_T max_system_mem;

  /* SRI: this arena's index. main arena = 1, non-main arena's 2, 3, .... */
  size_t arena_index;

  /* SRI: flag indicating arena is initialized */
  bool metadata_pool_ready;

  /* SRI: pool memory for the metadata */
  memcxt_t memcxt;

  /* SRI: metadata */
  metadata_t htbl;      

  /* SRI: metadata cache; pool of metadata big enough so 
     that we don't get caught halfway through an allocation routine 
     and not be able to create the necessary metadata. If we can't 
     fill the cache at the start of malloc, realloc, or calloc we
     can return 0 and know that the current state is still
     consistent.
  */
  chunkinfoptr  metadata_cache[METADATA_CACHE_SIZE];
  int           metadata_cache_count;

};



struct malloc_par
{
  /* Tunable parameters */
  unsigned long trim_threshold;
  INTERNAL_SIZE_T top_pad;
  INTERNAL_SIZE_T mmap_threshold;
  INTERNAL_SIZE_T arena_test;
  INTERNAL_SIZE_T arena_max;

  /* Memory map support */
  int n_mmaps;
  int n_mmaps_max;
  int max_n_mmaps;
  /* the mmap_threshold is dynamic, until the user sets
     it manually, at which point we need to disable any
     dynamic behavior. */
  int no_dyn_threshold;

  /* Statistics */
  INTERNAL_SIZE_T mmapped_mem;
  /*INTERNAL_SIZE_T  sbrked_mem;*/
  /*INTERNAL_SIZE_T  max_sbrked_mem;*/
  INTERNAL_SIZE_T max_mmapped_mem;
  INTERNAL_SIZE_T max_total_mem;  /* only kept for NO_THREADS */

  /* First address handed out by MORECORE/sbrk.  */
  char *sbrk_base;
};

/* There are several instances of this struct ("arenas") in this
   malloc.  If you are adapting this malloc in a way that does NOT use
   a static or mmapped malloc_state, you MUST explicitly zero-fill it
   before using. This malloc relies on the property that malloc_state
   is initialized to all zeroes (as is true of C statics).  */

static struct malloc_state main_arena =
  {
    .mutex = _LIBC_LOCK_INITIALIZER,
    .next = &main_arena,
    .attached_threads = 1
  };

/* There is only one instance of the malloc parameters.  */

static struct malloc_par mp_ =
  {
    .top_pad = DEFAULT_TOP_PAD,
    .n_mmaps_max = DEFAULT_MMAP_MAX,
    .mmap_threshold = DEFAULT_MMAP_THRESHOLD,
    .trim_threshold = DEFAULT_TRIM_THRESHOLD,
#define NARENAS_FROM_NCORES(n) ((n) * (sizeof (long) == 4 ? 2 : 8))
    .arena_test = NARENAS_FROM_NCORES (1)
  };


/* procedural abstraction */
static inline int arena_index(mstate av)
{
  return av->arena_index;
}


/*  Non public mallopt parameters.  */
#define M_ARENA_TEST -7
#define M_ARENA_MAX  -8


/* ---------------- Error behavior ------------------------------------ */

#ifndef DEFAULT_CHECK_ACTION
# define DEFAULT_CHECK_ACTION 3
#endif

static int check_action = DEFAULT_CHECK_ACTION;


/* Bins -- relocated to after definition of mstate */

/* addressing -- note that bin_at(0) does not exist */
static inline mbinptr bin_at(mstate av, int i)
{
  return &(av->bins[i]);
  //return (mbinptr) (((char *) &(av->bins[(i - 1) * 2])) - offsetof (struct malloc_chunk, fd));
}


/* analog of ++bin */
static inline mbinptr next_bin(mbinptr b)
{
  return b + 1; 
  //return ((mbinptr) ((char *)b + (sizeof (mchunkptr) << 1)));
}

/* Take a chunk off a bin list */
static inline void bin_unlink(mstate av, chunkinfoptr p, chunkinfoptr *bkp, chunkinfoptr *fdp) {
  *fdp = p->fd;
  *bkp = p->bk;
  if (__builtin_expect ((*fdp)->bk != p || (*bkp)->fd != p, 0))
    malloc_printerr (check_action, "corrupted double-linked list", p, av);
  else {
    (*fdp)->bk = *bkp;
    (*bkp)->fd = *fdp;
    if (!in_smallbin_range (p->size)
        && __builtin_expect (p->fd_nextsize != NULL, 0)) {
      if (__builtin_expect (p->fd_nextsize->bk_nextsize != p, 0)
          || __builtin_expect (p->bk_nextsize->fd_nextsize != p, 0))
        malloc_printerr (check_action,
                         "corrupted double-linked list (not small)",
                         p, av);
      if ((*fdp)->fd_nextsize == NULL) {
        if (p->fd_nextsize == p)
          (*fdp)->fd_nextsize = (*fdp)->bk_nextsize = *fdp;
        else {
          (*fdp)->fd_nextsize = p->fd_nextsize;
          (*fdp)->bk_nextsize = p->bk_nextsize;
          p->fd_nextsize->bk_nextsize = *fdp;
          p->bk_nextsize->fd_nextsize = *fdp;
        }
      } else {
        p->fd_nextsize->bk_nextsize = p->bk_nextsize;
        p->bk_nextsize->fd_nextsize = p->fd_nextsize;
      }
    }
  }
}



static inline bool in_smallbin_range(INTERNAL_SIZE_T sz)
{
  return  (unsigned long)sz < (unsigned long) MIN_LARGE_SIZE;
}

static inline unsigned int smallbin_index(INTERNAL_SIZE_T sz)
{
  if (SMALLBIN_WIDTH == 16) {
    return ((unsigned int)sz) >> 4;
  } else {
    return (((unsigned int)sz) >> 3) + SMALLBIN_CORRECTION;
  }
}

static inline unsigned int largebin_index_32(INTERNAL_SIZE_T sz)
{
  return (((((unsigned long) sz) >> 6) <= 38) ?  56 + (((unsigned long) sz) >> 6) :
          ((((unsigned long) sz) >> 9) <= 20) ?  91 + (((unsigned long) sz) >> 9) :
          ((((unsigned long) sz) >> 12) <= 10) ? 110 + (((unsigned long) sz) >> 12) :
          ((((unsigned long) sz) >> 15) <= 4) ? 119 + (((unsigned long) sz) >> 15) :
          ((((unsigned long) sz) >> 18) <= 2) ? 124 + (((unsigned long) sz) >> 18) :
          126);
} 


static inline unsigned int largebin_index_32_big(INTERNAL_SIZE_T sz)
{
  return (((((unsigned long) sz) >> 6) <= 45) ?  49 + (((unsigned long) sz) >> 6) :
          ((((unsigned long) sz) >> 9) <= 20) ?  91 + (((unsigned long) sz) >> 9) :
          ((((unsigned long) sz) >> 12) <= 10) ? 110 + (((unsigned long) sz) >> 12) :
          ((((unsigned long) sz) >> 15) <= 4) ? 119 + (((unsigned long) sz) >> 15) :
          ((((unsigned long) sz) >> 18) <= 2) ? 124 + (((unsigned long) sz) >> 18) :
          126);
}

// XXX It remains to be seen whether it is good to keep the widths of
// XXX the buckets the same or whether it should be scaled by a factor
// XXX of two as well.
static inline unsigned int largebin_index_64(INTERNAL_SIZE_T sz)
{
  return (((((unsigned long) sz) >> 6) <= 48) ?  48 + (((unsigned long) sz) >> 6) :
          ((((unsigned long) sz) >> 9) <= 20) ?  91 + (((unsigned long) sz) >> 9) :
          ((((unsigned long) sz) >> 12) <= 10) ? 110 + (((unsigned long) sz) >> 12) :
          ((((unsigned long) sz) >> 15) <= 4) ? 119 + (((unsigned long) sz) >> 15) :
          ((((unsigned long) sz) >> 18) <= 2) ? 124 + (((unsigned long) sz) >> 18) :
          126);
}

static inline unsigned int largebin_index(INTERNAL_SIZE_T sz)
{
  return (SIZE_SZ == 8 ? largebin_index_64 (sz)
          : MALLOC_ALIGNMENT == 16 ? largebin_index_32_big (sz)
          : largebin_index_32 (sz));
}

static inline unsigned int bin_index(INTERNAL_SIZE_T sz)
{
  return ((in_smallbin_range (sz)) ? smallbin_index (sz) : largebin_index (sz));
}

static inline void mark_bin(mstate av, int i)
{
  av->binmap[idx2block(i)] |= idx2bit(i);
}


static inline void unmark_bin(mstate av, int i)
{
  av->binmap[idx2block(i)] &= ~idx2bit(i);
}

static inline unsigned int get_binmap(mstate av, int i)
{
  return av->binmap[idx2block(i)] & idx2bit (i);
}

/* Maximum size of memory handled in fastbins.  */
static INTERNAL_SIZE_T global_max_fast;

static inline bool have_fastchunks(mstate av)
{
  return (av->flags & FASTCHUNKS_BIT) == 0;
}

static inline void clear_fastchunks(mstate av)
{
  catomic_or (&av->flags, FASTCHUNKS_BIT);
}

static inline void set_fastchunks(mstate av)
{
  catomic_and (&av->flags, ~FASTCHUNKS_BIT);
}

static inline bool contiguous(mstate av)
{
  return (av->flags & NONCONTIGUOUS_BIT) == 0;
}

static inline bool noncontiguous(mstate av)
{
  return (av->flags & NONCONTIGUOUS_BIT) != 0;
}

static inline void set_noncontiguous(mstate av)
{
  av->flags |= NONCONTIGUOUS_BIT;
}

static inline void set_contiguous(mstate av)
{
  av->flags &= ~NONCONTIGUOUS_BIT;
}

static inline bool arena_is_corrupt(mstate av)
{
  return av->flags & ARENA_CORRUPTION_BIT;
}
static inline void set_arena_corrupt(mstate av)
{
  av->flags |= ARENA_CORRUPTION_BIT;
}

static void set_max_fast(INTERNAL_SIZE_T sz)
{
  if (sz == 0) {
    global_max_fast = SMALLBIN_WIDTH;
  } else {
    global_max_fast = ((sz + SIZE_SZ) & ~MALLOC_ALIGN_MASK);
  }
}

static INTERNAL_SIZE_T get_max_fast(void)
{
  return global_max_fast;
}

/*
  Initialize a malloc_state struct.

  This is called only from within malloc_consolidate, which needs
  be called in the same contexts anyway.  It is never called directly
  outside of malloc_consolidate because some optimizing compilers try
  to inline it at all call points, which turns out not to be an
  optimization at all. (Inlining it in malloc_consolidate is fine though.)
*/

static inline chunkinfoptr initial_md_top(mstate av)
{
  mchunkptr top = &(av->initial_top);
  chunkinfoptr _md_top = register_chunk(av, top, false, 0);
  //Done: _md_top->md_prev = _md_top->md_next = NULL
  _md_top->prev_size = 0;
  set_head(_md_top, 0);
  return _md_top;
}

#include "debug.h"

static void
malloc_init_state (mstate av, bool is_main_arena)
{
  int i;
  mbinptr bin;

  if(is_main_arena){
    log_init();
    lookup_init();
  }

  /* Establish circular links for normal bins */
  for (i = 1; i < NBINS; ++i)
    {
      bin = bin_at (av, i);
      bin->fd = bin->bk = bin;
    }

#if MORECORE_CONTIGUOUS
  if (av != &main_arena)
#endif
    set_noncontiguous (av);
  if (av == &main_arena)
    set_max_fast (DEFAULT_MXFAST);
  av->flags |= FASTCHUNKS_BIT;

  if(is_main_arena){
    /* the main arena has arena index 1. */
    av->arena_index = MAIN_ARENA_INDEX;
  } else {
    /* non-main arena's get assigned their index in arena.c's _int_new_arena */
    av->arena_index = 0;
  }

  /* init the metadata pool */
  init_memcxt(&av->memcxt);

  /* init the metadata hash table */
  if ( ! init_metadata(&av->htbl, &av->memcxt)) {
    abort();
  }

  /* enable replenishing */
  av->metadata_pool_ready = true;


  if ( ! replenish_metadata_cache(av) ) {
    abort();
  }
  
  av->_md_top = initial_md_top(av);
}

static bool replenish_metadata_cache(mstate av)
{
  int i;
  chunkinfoptr _md_p;
  int count = av->metadata_cache_count;

  /* we only replenish if av has already been initialized */
  if ( ! av->metadata_pool_ready) {  return true; }

  assert(METADATA_CACHE_SIZE >= count && count >= 0);

  if(count < METADATA_CACHE_SIZE){
    for(i = count; i < METADATA_CACHE_SIZE; i++) {
      assert(av->metadata_cache[i] == NULL);
      _md_p = allocate_chunkinfoptr(&(av->htbl));
      if (_md_p != NULL) {
	av->metadata_cache[i] = _md_p;
      } else {
	return false;
      }
    }
    av->metadata_cache_count = METADATA_CACHE_SIZE;
  }

  return true;
}




/*
  Other internal utilities operating on mstates
*/

static chunkinfoptr sysmalloc (INTERNAL_SIZE_T, mstate);
static int   systrim (size_t, mstate);
static void  malloc_consolidate (mstate);

/*
  ----------- SRI: Metadata manipulation and initialization -----------
*/

static inline INTERNAL_SIZE_T size2chunksize(INTERNAL_SIZE_T sz)
{
  return ( sz & ~(SIZE_BITS));
}

/* 
   Get a free chunkinfo from av.  In the current implementation
   the cache is only used as a last resort. 
*/
static chunkinfoptr new_chunkinfoptr(mstate av)
{
  chunkinfoptr retval;
  assert(av != NULL);

  assert(av->metadata_cache_count > 0);
  if(av->metadata_cache_count <= 0){
    abort();
  }
  
  retval = allocate_chunkinfoptr(&(av->htbl));
  
  if (retval != NULL){ return retval; }
  
  /*
    SRI: if we want to push the "replenish" down to _int_malloc, then we 

    need to handle the mremap path in libc_realloc. the easiest way to
    do this is to relegate the cache to a last line of defense mechanism
    and first just try the get the metadata with allocate_chunkinfoptr.
    if that fails, then we use the cache.
  */
  
  retval = av->metadata_cache[--av->metadata_cache_count];
  av->metadata_cache[av->metadata_cache_count] = NULL;
  
  return retval;
}

/* lookup the chunk in the hashtable */
static chunkinfoptr
lookup_chunk (mstate av, mchunkptr p)
{
  assert(av != NULL);
  assert(p != NULL);
  return metadata_lookup(&av->htbl, chunk2mem(p));
}


/* Remove the metadata from the hashtable */
static bool
unregister_chunk (mstate av, mchunkptr p, int tag) 
{
  assert(av != NULL);
  assert(p != NULL);

#ifdef SRI_DEBUG
  if(tag){
    p->__canary__ = tag;
  }
#endif

  return metadata_delete(&av->htbl, chunk2mem(p));
}

static mchunkptr chunkinfo2chunk(chunkinfoptr _md_victim)
{
  assert(_md_victim != NULL);
  return mem2chunk(_md_victim->chunk);
}

static void* chunkinfo2mem(chunkinfoptr _md_victim)
{
  if (_md_victim == NULL) {
    return 0;
  } else {
    return _md_victim->chunk;
  }
}

static chunkinfoptr register_chunk(mstate av, mchunkptr p, bool is_mmapped, int tag)
{
  chunkinfoptr _md_p = new_chunkinfoptr(av);
  bool success;
  
  size_t a_idx = is_mmapped ? MMAPPED_ARENA_INDEX : arena_index(av);
  _md_p->chunk = chunk2mem(p);
  
#ifdef SRI_DEBUG
  p->__canary__ = 123456789000 + tag;
#endif
  
  set_arena_index(av, p, a_idx);
  success = metadata_add(&av->htbl, _md_p);
  assert(success);
  if( ! success ) { return NULL; }
  
  return _md_p;
}


static inline INTERNAL_SIZE_T chunksize(chunkinfoptr ci)
{
  if (ci == NULL)
    return 0;
  else 
    return (ci->size & ~(SIZE_BITS));
}

#define missing_metadata(AV,P) report_missing_metadata(AV, P, __FILE__, __LINE__)

static void report_missing_metadata(mstate av, mchunkptr p, const char* file, int lineno)
{
  fprintf(stderr, "No metadata for %p. main_arena %d. chunk_is_mmapped: %d @ %s line %d\n", 
          chunk2mem(p), av == &main_arena, chunk_is_mmapped(p), file, lineno);
  abort();
}



/* sanity check */
static bool do_check_metadata_chunk(mstate av, mchunkptr c, chunkinfoptr ci, const char* file, int lineno)
{
  if (ci != NULL) {
    if (chunkinfo2chunk(ci) != c) {
      fprintf(stderr, "check_metadata_chunk of %p:\nmetadata and data do not match @ %s line %d\n",
              chunk2mem(c), file, lineno);
      assert(false);
      return false;
    }
    
    if(ci->md_next != NULL){
      mchunkptr cn = chunkinfo2chunk(ci->md_next);
      if(ci->md_next->md_prev != ci){
      fprintf(stderr, 
	      "check_metadata_chunk of %p in arena %zu with canary %zu @ %s line %d:\n" 
	      "\tci->md_next->md_prev = %p != ci = %p\n"
	      "\tci->md_next = %p with canary %zu\n",
              chunk2mem(c), c->arena_index, c->__canary__, file, lineno, 
	      ci->md_next->md_prev, ci->md_next, 
	      ci, cn->__canary__);
      assert(false);
      return false;
      }
    }

    if(ci->md_prev != NULL){
      mchunkptr cp = chunkinfo2chunk(ci->md_prev);
      if(ci->md_prev->md_next != ci){
	fprintf(stderr, 
		"check_metadata_chunk of %p in arena %zu with canary %zu @ %s line %d:\n"
		"\tci->md_prev->md_next = %p != ci = %p\n"
		"\tci->md_prev = %p with canary %zu\n",
		chunk2mem(c), c->arena_index, c->__canary__, file, lineno, 
		ci->md_prev->md_next, ci->md_prev,
		ci, cp->__canary__);
	assert(false);
	return false;
      }
    }
    

    return true;
  }
  assert(false);
  return false;
}


/* -------------- Early definitions for debugging hooks ---------------- */

/* Define and initialize the hook variables.  These weak definitions must
   appear before any use of the variables in a function (arena.c uses one).  */
#ifndef weak_variable
/* In GNU libc we want the hook variables to be weak definitions to
   avoid a problem with Emacs.  */
# define weak_variable weak_function
#endif

/* Forward declarations.  */
static void *malloc_hook_ini (size_t sz,
                              const void *caller) __THROW;
static void *realloc_hook_ini (void *ptr, size_t sz,
                               const void *caller) __THROW;
static void *memalign_hook_ini (size_t alignment, size_t sz,
                                const void *caller) __THROW;

void weak_variable (*__malloc_initialize_hook) (void) = NULL;
void weak_variable (*__free_hook) (void *__ptr,
                                   const void *) = NULL;
void *weak_variable (*__malloc_hook)
  (size_t __size, const void *) = malloc_hook_ini;
void *weak_variable (*__realloc_hook)
  (void *__ptr, size_t __size, const void *)
= realloc_hook_ini;
void *weak_variable (*__memalign_hook)
  (size_t __alignment, size_t __size, const void *)
= memalign_hook_ini;
void weak_variable (*__after_morecore_hook) (void) = NULL;


/* ------------------ Testing support ----------------------------------*/

static int perturb_byte;

static void
alloc_perturb (char *p, size_t n)
{
  if (__glibc_unlikely (perturb_byte))
    memset (p, perturb_byte ^ 0xff, n);
}

static void
free_perturb (char *p, size_t n)
{
  if (__glibc_unlikely (perturb_byte))
    memset (p, perturb_byte, n);
}



#include <stap-probe.h>

/* ------------------- Support for multiple arenas -------------------- */

/* used in arena.c as well as here in malloc.c */
static void do_check_top(mstate av, const char* file, int lineno);

/* lock logging macros */

#include "locking.h"

#include "arena.c"

/*
  Debugging support

  These routines make a number of assertions about the states
  of data structures that should be true at all times. If any
  are not true, it's very likely that a user program has somehow
  trashed memory. (It's also possible that there is a coding error
  in malloc. In which case, please report it!)
*/

static void do_check_top(mstate av, const char* file, int lineno)
{
  if (av->_md_top) {
    if ( !do_check_metadata_chunk(av, chunkinfo2chunk(av->_md_top), av->_md_top, file, lineno)) {
      fprintf(stderr, "check top failed @ %s line %d\n", file, lineno);
      assert(false);
    }
  }
}


#if !MALLOC_DEBUG

# define check_top(A)
# define check_chunk(A, P, MD_P)
# define check_free_chunk(A, P, MD_P)
# define check_inuse_chunk(A, P, MD_P)
# define check_remalloced_chunk(A, P, MD_P, N)
# define check_malloced_chunk(A, P, MD_P, N)
# define check_malloc_state(A)
# define check_metadata_chunk(A,P,MD_P)        do_check_metadata_chunk(A,P,MD_P,__FILE__,__LINE__)
# define check_metadata(A,MD_P)                do_check_metadata_chunk(A,chunkinfo2chunk(MD_P),MD_P,__FILE__,__LINE__)

#else

# define check_top(A)                           do_check_top(A,__FILE__,__LINE__)
# define check_chunk(A, P, MD_P)                do_check_chunk (A, P, MD_P,__FILE__,__LINE__)
# define check_free_chunk(A, P, MD_P)           do_check_free_chunk (A, P, MD_P,__FILE__,__LINE__)
# define check_inuse_chunk(A, P, MD_P)          do_check_inuse_chunk (A, P, MD_P,__FILE__,__LINE__)
# define check_remalloced_chunk(A, P, MD_P, N)  do_check_remalloced_chunk (A, P, MD_P, N,__FILE__,__LINE__)
# define check_malloced_chunk(A, P, MD_P, N)    do_check_malloced_chunk (A, P, MD_P, N,__FILE__,__LINE__)
# define check_malloc_state(A)                  do_check_malloc_state (A,__FILE__,__LINE__)
# define check_metadata_chunk(A,P,MD_P)         do_check_metadata_chunk(A,P,MD_P,__FILE__,__LINE__)
# define check_metadata(A,MD_P)                 do_check_metadata_chunk(A,chunkinfo2chunk(MD_P),MD_P,__FILE__,__LINE__)

/*
  Properties of all chunks
*/

static void
do_check_chunk (mstate av, mchunkptr p, chunkinfoptr _md_p, const char* file, int lineno)
{
  unsigned long sz;
  char *max_address;
  char *min_address;
  bool metadata_ok;
  mchunkptr topchunk;

  /* no crashing in debugging when we are running out of memory */
  if(av == NULL){ return; }

  assert(chunk_is_mmapped (p) || (av == arena_from_chunk(p)));

  sz = chunksize (_md_p);
  
  topchunk = chunkinfo2chunk(av->_md_top);

  /* min and max possible addresses assuming contiguous allocation */
  max_address = (char *) (topchunk) + chunksize (av->_md_top);
  min_address = max_address - av->system_mem;

  check_top(av);

  metadata_ok = do_check_metadata_chunk(av, p, _md_p, file, lineno);

  assert(metadata_ok);
  if (!metadata_ok) {
    fprintf(stderr,  "do_check_chunk: metadata_ok, ...not. %s:%d\n", file, lineno);
  }


  if (!chunk_is_mmapped (p))
    {
      /* Has legal address ... */
      if (p != topchunk)
        {
          if (contiguous (av))
            {
              assert (((char *) p) >= min_address);
              assert (((char *) p + sz) <= ((char *) (topchunk)));
            }
        }
      else
        {
          /* top size is always at least MINSIZE */
          assert ((unsigned long) (sz) >= MINSIZE);
          /* top predecessor always marked inuse */
          assert (prev_inuse(_md_p, p));
        }
    }
  else
    {
      /* address is outside main heap  */
      if (contiguous (av) && topchunk != &av->initial_top)
        {
          assert (((char *) p) < min_address || ((char *) p) >= max_address);
        }
      /* chunk is page-aligned */
      assert (((_md_p->prev_size + sz) & (GLRO (dl_pagesize) - 1)) == 0);
      /* mem is aligned */
      assert (aligned_OK ((unsigned long)chunk2mem (p)));
    }
}

/*
  Properties of free chunks
*/

static void
do_check_free_chunk (mstate av, mchunkptr p, chunkinfoptr _md_p, const char* file, int lineno)
{
  INTERNAL_SIZE_T sz;
  mchunkptr next, topchunk;
  chunkinfoptr _md_next;
  
  /* no debugging when we are running out of memory */
  if(av == NULL){ return; }

  do_check_chunk (av, p, _md_p, file, lineno);

  topchunk = chunkinfo2chunk(av->_md_top);
  
  sz = _md_p->size & ~PREV_INUSE;
  next = chunk_at_offset (p, sz);
  _md_next = lookup_chunk(av, next);
  if (_md_next == NULL) { missing_metadata(av, next); }
  
  /* Chunk must claim to be free ... */
  assert (!inuse(av, _md_p, p));
  assert (!chunk_is_mmapped (p));

  /* Unless a special marker, must have OK fields */
  if ((unsigned long) (sz) >= MINSIZE)
    {
      assert ((sz & MALLOC_ALIGN_MASK) == 0);
      assert (aligned_OK ((unsigned long)chunk2mem (p)));
      /* ... matching footer field */
      assert (_md_next->prev_size == sz);
      /* ... and is fully consolidated */
      assert (prev_inuse(_md_p, p));
      assert (next == topchunk || inuse(av, _md_next, next));

      /* ... and has minimally sane links */
      assert (_md_p->fd->bk == _md_p);
      assert (_md_p->bk->fd == _md_p);
    }
  else /* markers are always of size SIZE_SZ */
    assert (sz == SIZE_SZ);
}

/*
  Properties of inuse chunks
*/

static void
do_check_inuse_chunk (mstate av, mchunkptr p, chunkinfoptr _md_p, const char* file, int lineno)
{
  mchunkptr next;
  chunkinfoptr _md_next;
  mchunkptr prv;
  chunkinfoptr _md_prv;
  mchunkptr topchunk;

  /* no debugging when we are running out of memory */
  if(av == NULL){ return; }

  do_check_chunk (av, p, _md_p, file, lineno);

  topchunk = chunkinfo2chunk(av->_md_top);

  if (chunk_is_mmapped (p))
    return; /* mmapped chunks have no next/prev */

  /* Check whether it claims to be in use ... */
  assert (inuse(av, _md_p, p));

  next = next_chunk (_md_p, p);
  _md_next = lookup_chunk(av, next);
  if (_md_next == NULL) { missing_metadata(av, next); }


  /* ... and is surrounded by OK chunks.
     Since more things can be checked with free chunks than inuse ones,
     if an inuse chunk borders them and debug is on, it's worth doing them.
  */
  if (!prev_inuse(_md_p, p))
    {
      /* Note that we cannot even look at prev unless it is not inuse */
      prv = prev_chunk (_md_p, p);
      _md_prv = lookup_chunk(av, prv);
      if (_md_prv == NULL) { missing_metadata(av, prv); }
      assert (next_chunk (_md_prv, prv) == p);
      do_check_free_chunk (av, prv, _md_prv, file, lineno);
    }

  if (next == topchunk)
    {
      assert (prev_inuse(_md_next, next));
      assert (chunksize (_md_next) >= MINSIZE);
    }
  else if (!inuse(av, _md_next, next))
    do_check_free_chunk (av, next, _md_next, file, lineno);
}

/*
  Properties of chunks recycled from fastbins
*/

static void
do_check_remalloced_chunk (mstate av, mchunkptr p, chunkinfoptr _md_p, 
                           INTERNAL_SIZE_T s, const char* file, int lineno)
{
  INTERNAL_SIZE_T sz;

  /* no debugging when we are running out of memory */
  if(av == NULL){ return; }

  sz = _md_p->size & ~PREV_INUSE;

  if (!chunk_is_mmapped (p))
    {
      assert (av == arena_for_chunk (p));
      if (chunk_non_main_arena (p))
        assert (av != &main_arena);
      else
        assert (av == &main_arena);
    }

  do_check_inuse_chunk (av, p, _md_p, file, lineno);

  /* Legal size ... */
  assert ((sz & MALLOC_ALIGN_MASK) == 0);
  assert ((unsigned long) (sz) >= MINSIZE);
  /* ... and alignment */
  assert (aligned_OK ((unsigned long)chunk2mem (p)));
  /* chunk is less than MINSIZE more than request */
  assert ((long) (sz) - (long) (s) >= 0);
  assert ((long) (sz) - (long) (s + MINSIZE) < 0);
}

/*
  Properties of nonrecycled chunks at the point they are malloced
*/

static void
do_check_malloced_chunk (mstate av, mchunkptr p, chunkinfoptr _md_p, INTERNAL_SIZE_T s, const char* file, int lineno)
{

  /* no debugging when we are running out of memory */
  if(av == NULL){ return; }

  /* same as recycled case ... */
  do_check_remalloced_chunk (av, p, _md_p, s, file, lineno);

  /*
    ... plus,  must obey implementation invariant that prev_inuse is
    always true of any allocated chunk; i.e., that each allocated
    chunk borders either a previously allocated and still in-use
    chunk, or the base of its memory arena. This is ensured
    by making all allocations from the `lowest' part of any found
    chunk.  This does not necessarily hold however for chunks
    recycled via fastbins.
  */

  assert (prev_inuse(_md_p, p));
}


/*
  Properties of malloc_state.

  This may be useful for debugging malloc, as well as detecting user
  programmer errors that somehow write into malloc_state.

  If you are extending or experimenting with this malloc, you can
  probably figure out how to hack this routine to print out or
  display chunk addresses, sizes, bins, and other instrumentation.
*/

static void
do_check_malloc_state (mstate av, const char* file, int lineno)
{
  int i;

  chunkinfoptr _md_p;
  chunkinfoptr _md_q;

  mchunkptr p;
  mchunkptr q;

  mbinptr b;
  unsigned int idx;
  INTERNAL_SIZE_T size;
  unsigned long total = 0;
  int max_fast_bin;

  mchunkptr topchunk;

  /* no debugging when we are running out of memory */
  if(av == NULL){ return; }

  topchunk = chunkinfo2chunk(av->_md_top);

  /* internal size_t must be no wider than pointer type */
  assert (sizeof (INTERNAL_SIZE_T) <= sizeof (char *));

  /* alignment is a power of 2 */
  assert ((MALLOC_ALIGNMENT & (MALLOC_ALIGNMENT - 1)) == 0);

  /* cannot run remaining checks until fully initialized */
  if (topchunk == 0 || topchunk == &av->initial_top)
    return;

  /* pagesize is a power of 2 */
  assert (powerof2(GLRO (dl_pagesize)));

  /* A contiguous main_arena is consistent with sbrk_base.  */
  if (av == &main_arena && contiguous (av))
    assert ((char *) mp_.sbrk_base + av->system_mem ==
            (char *) topchunk + chunksize (av->_md_top));

  /* properties of fastbins */

  /* max_fast is in allowed range */
  assert ((get_max_fast () & ~1) <= request2size (MAX_FAST_SIZE));

  max_fast_bin = fastbin_index (get_max_fast ());

  for (i = 0; i < NFASTBINS; ++i)
    {
      _md_p = fastbin (av, i);

      /* The following test can only be performed for the main arena.
         While mallopt calls malloc_consolidate to get rid of all fast
         bins (especially those larger than the new maximum) this does
         only happen for the main arena.  Trying to do this for any
         other arena would mean those arenas have to be locked and
         malloc_consolidate be called for them.  This is excessive.  And
         even if this is acceptable to somebody it still cannot solve
         the problem completely since if the arena is locked a
         concurrent malloc call might create a new arena which then
         could use the newly invalid fast bins.  */

      /* all bins past max_fast are empty */
      if (av == &main_arena && i > max_fast_bin)
        assert (_md_p == 0);

      while (_md_p != 0)
        {
	  p = chunkinfo2chunk(_md_p);
          /* each chunk claims to be inuse */
          do_check_inuse_chunk (av, p, _md_p, file, lineno);
          total += chunksize (_md_p);
          /* chunk belongs in this bin */
          assert (fastbin_index (chunksize (_md_p)) == i);
          _md_p = _md_p->fd;
        }
    }

  if (total != 0)
    assert (have_fastchunks (av));
  else if (!have_fastchunks (av))
    assert (total == 0);

  /* check normal bins */
  for (i = 1; i < NBINS; ++i)
    {
      b = bin_at (av, i);

      /* binmap is accurate (except for bin 1 == unsorted_chunks) */
      if (i >= 2)
        {
          unsigned int binbit = get_binmap (av, i);
          int empty = last (b) == b;
          if (!binbit)
            assert (empty);
          else if (!empty)
            assert (binbit);
        }

      for (_md_p = last (b); _md_p != b; _md_p = _md_p->bk)
        {
          /* each chunk claims to be free */
	  p = chunkinfo2chunk(_md_p);
          do_check_free_chunk (av, p, _md_p, file, lineno);
          size = chunksize (_md_p);
          total += size;
          if (i >= 2)
            {
              /* chunk belongs in bin */
              idx = bin_index (size);
              assert (idx == i);
              /* lists are sorted */
              assert (_md_p->bk == b ||
                      (unsigned long) chunksize (_md_p->bk) >= (unsigned long) chunksize (_md_p));

              if (!in_smallbin_range (size))
                {
                  if (_md_p->fd_nextsize != NULL)
                    {
                      if (_md_p->fd_nextsize == _md_p)
                        assert (_md_p->bk_nextsize == _md_p);
                      else
                        {
                          if (_md_p->fd_nextsize == first (b))
                            assert (chunksize (_md_p) < chunksize (_md_p->fd_nextsize));
                          else
                            assert (chunksize (_md_p) > chunksize (_md_p->fd_nextsize));

                          if (_md_p == first (b))
                            assert (chunksize (_md_p) > chunksize (_md_p->bk_nextsize));
                          else
                            assert (chunksize (_md_p) < chunksize (_md_p->bk_nextsize));
                        }
                    }
                  else
                    assert (_md_p->bk_nextsize == NULL);
                }
            }
          else if (!in_smallbin_range (size))
            assert (_md_p->fd_nextsize == NULL && _md_p->bk_nextsize == NULL);

          /* chunk is followed by a legal chain of inuse chunks */
	  q = next_chunk(_md_p, p);
	  _md_q = lookup_chunk(av, q);
	  if (_md_q == NULL) { missing_metadata(av, q); }
	  while(q != topchunk && inuse(av, _md_q, q) && (unsigned long)(chunksize(_md_q)) >= MINSIZE){
	    do_check_inuse_chunk(av, q, _md_q, file, lineno);
	    q = next_chunk(_md_q, q);
	    _md_q = lookup_chunk(av, q);
	  }

        }
    }

  /* top chunk is OK */
  check_chunk (av, topchunk, av->_md_top);
}
#endif


/* ----------------- Support for debugging hooks -------------------- */
#include "hooks.c"


/* ----------- Routines dealing with system allocation -------------- */

/* Splits victim into a chunk of size 'desiderata' and returns the
   configured metadata of the remainder.  Called in the situation
   where victim is top.
*/
static chunkinfoptr split_chunk(mstate av, 
                                chunkinfoptr _md_victim, mchunkptr victim, INTERNAL_SIZE_T victim_size, 
                                INTERNAL_SIZE_T desiderata)
{
  INTERNAL_SIZE_T remainder_size;
  mchunkptr remainder; 
  chunkinfoptr _md_remainder;

  assert(chunksize(_md_victim) == victim_size);
  assert((unsigned long)victim_size >= (unsigned long)(desiderata + MINSIZE)); //iam: why the casts?

  /* configure the remainder */
  remainder_size = victim_size - desiderata;
  remainder = chunk_at_offset(victim, desiderata);

  /* pair it with new metatdata and add the metadata into the hashtable */
  _md_remainder = register_chunk(av, remainder, false, 1);

  /* fix the next and prev metadata pointers */
  _md_remainder->md_next = _md_victim->md_next;   // should be NULL
  _md_remainder->md_prev = _md_victim;
  _md_victim->md_next = _md_remainder;

  /* set its size */
  set_head(_md_remainder, remainder_size | PREV_INUSE);

  /* configure the victim */
  set_head(_md_victim,  desiderata | PREV_INUSE);

  check_metadata_chunk(av,remainder,_md_remainder);
  check_metadata_chunk(av,victim,_md_victim);


  return _md_remainder;
}

/*
  sysmalloc handles malloc cases requiring more memory from the system.
  On entry, it is assumed that av->_md_top does not have enough
  space to service request for nb bytes, thus requiring that av->_md_top
  be extended or replaced.
*/

static chunkinfoptr
sysmalloc (INTERNAL_SIZE_T nb, mstate av)
{
  mchunkptr old_top;              /* incoming value of top chunk   */
  chunkinfoptr _md_old_top;       /* incoming value of av->_md_top */
  INTERNAL_SIZE_T old_size;       /* its size */
  char *old_end;                  /* its end address */

  long size;                      /* arg to first MORECORE or mmap call */
  char *brk;                      /* return value from MORECORE */

  long correction;                /* arg to 2nd MORECORE call */
  char *snd_brk;                  /* 2nd return val */

  char *mbrk;                     /* start of the mmapped extension to sbrked memory */

  INTERNAL_SIZE_T front_misalign; /* unusable bytes at front of new space */
  INTERNAL_SIZE_T end_misalign;   /* partial page left at end of new space */
  char *aligned_brk;              /* aligned offset into brk */

  mchunkptr p;                    /* the allocated/returned chunk */
  chunkinfoptr _md_p;             /* metadata of the allocated/returned chunk */

  mchunkptr fencepost_0;          /* fenceposts */
  chunkinfoptr _md_fencepost_0;   /* metadata of fenceposts */

  mchunkptr fencepost_1;          /* fenceposts */
  chunkinfoptr _md_fencepost_1;   /* metadata of fenceposts */

  mchunkptr topchunk;             /* new chunk destined for top */


  size_t pagesize = GLRO (dl_pagesize);
  bool tried_mmap = false;

  bool have_switched_lock = false;
  

  /*
    If have mmap, and the request size meets the mmap threshold, and
    the system supports mmap, and there are few enough currently
    allocated mmapped regions, try to directly map this request
    rather than expanding top.
  */

  if (av == NULL
      || ((unsigned long) (nb) >= (unsigned long) (mp_.mmap_threshold)
          && (mp_.n_mmaps < mp_.n_mmaps_max)))
    {
      char *mm;           /* return value from mmap call*/

    try_mmap:

      /*
	SRI: we are going to mmap, so we need to make sure that we have 
	metadata for the new chunk. Thus we will need to lock the main arena,
	check its cache (perhaps replenish if possible), or fail, and then mmap the region.
       */
      
      if (av  != &main_arena) {
	/* if we are already in the main arena we should, by design, have enough metadata */
	if(av != NULL){
	  UNLOCK_ARENA(av, SYSMALLOC_SITE);
	}
	LOCK_ARENA(&main_arena, SYSMALLOC_SITE);
	
	have_switched_lock = true;
	
	if(main_arena.metadata_cache_count == 0){
	  UNLOCK_ARENA(&main_arena, SYSMALLOC_SITE);

	  have_switched_lock = false;
	  
	  if(av != NULL){
	    LOCK_ARENA(av, SYSMALLOC_SITE);
	  }

	  return NULL;
	}
      }
      
      /*
        Round up size to nearest page.  For mmapped chunks, the overhead
        is one SIZE_SZ unit larger than for normal chunks, because there
        is no following chunk whose prev_size field could be used.

        See the front_misalign handling below, for glibc there is no
        need for further alignments unless we have have high alignment.
      */
      if (MALLOC_ALIGNMENT == 2 * SIZE_SZ)
        size = ALIGN_UP (nb + SIZE_SZ, pagesize);
      else
        size = ALIGN_UP (nb + SIZE_SZ + MALLOC_ALIGN_MASK, pagesize);
      tried_mmap = true;

      /* Don't try if size wraps around 0 */
      if ((unsigned long) (size) > (unsigned long) (nb))
        {
          mm = (char *) (sys_MMAP (0, size, PROT_READ | PROT_WRITE, 0));

          if (mm != MAP_FAILED)
            {
              /*
                The offset to the start of the mmapped region is stored
                in the prev_size field of the chunk. This allows us to adjust
                returned start address to meet alignment requirements here
                and in memalign(), and still be able to compute proper
                address argument for later munmap in free() and realloc().
              */

              if (MALLOC_ALIGNMENT == 2 * SIZE_SZ)
                {
                  /* For glibc, chunk2mem increases the address by 2*SIZE_SZ and
                     MALLOC_ALIGN_MASK is 2*SIZE_SZ-1.  Each mmap'ed area is page
                     aligned and therefore definitely MALLOC_ALIGN_MASK-aligned.  */
                  assert (((INTERNAL_SIZE_T) chunk2mem (mm) & MALLOC_ALIGN_MASK) == 0);
                  front_misalign = 0;
                }
              else
                front_misalign = (INTERNAL_SIZE_T) chunk2mem (mm) & MALLOC_ALIGN_MASK;

              if (front_misalign > 0)
                {
                  correction = MALLOC_ALIGNMENT - front_misalign;
                  p = (mchunkptr) (mm + correction);
                }
              else
                {
                  p = (mchunkptr) mm;
                }
              
              /* SRI: the main_arena has jurisdiction over mmapped memory */
	      _md_p = register_chunk(&main_arena, p, true, 2);
	      check_metadata_chunk(av,p,_md_p);

              if (front_misalign > 0)
                {
                  _md_p->prev_size = correction;
                  set_head (_md_p, (size - correction));
                }
              else
                {
                  set_head (_md_p, size);
                }
	      
	      lookup_add_mmap(p, size);

	      
	      
	      /* update statistics */

              int new = atomic_exchange_and_add (&mp_.n_mmaps, 1) + 1;
              atomic_max (&mp_.max_n_mmaps, new);

              unsigned long sum;
              sum = atomic_exchange_and_add (&mp_.mmapped_mem, size) + size;
              atomic_max (&mp_.max_mmapped_mem, sum);

              check_chunk (av, p, _md_p);
	      
	      /* restore the state of the locks */
	      UNLOCK_ARENA(&main_arena, SYSMALLOC_SITE);

	      have_switched_lock = false;


	      if(av != NULL){
		LOCK_ARENA(av, SYSMALLOC_SITE);
	      }
              return _md_p;
            }

        }
	
      
    } /* end of try_mmap */

  if( have_switched_lock){

    /* restore the state of the locks */
    UNLOCK_ARENA(&main_arena, SYSMALLOC_SITE);
    
    have_switched_lock = false;
    
    
    if(av != NULL){
      LOCK_ARENA(av, SYSMALLOC_SITE);
    }
    	
  }

  /* There are no usable arenas and mmap also failed.  */
  if (av == NULL){
    return 0;
  }

  check_malloc_state (av);

  /* Record incoming configuration of top */

  _md_old_top = av->_md_top;
  old_top = chunkinfo2chunk(_md_old_top );
  old_size = chunksize (_md_old_top);
  old_end = (char *) (chunk_at_offset (old_top, old_size));

  brk = snd_brk = (char *) (MORECORE_FAILURE);
  mbrk = NULL;

  /*
    If not the first time through, we require old_size to be
    at least MINSIZE and to have prev_inuse set.
  */

  assert ((old_top == &av->initial_top && old_size == 0) ||
          ((unsigned long) (old_size) >= MINSIZE &&
           prev_inuse(_md_old_top, old_top) &&
           ((unsigned long) old_end & (pagesize - 1)) == 0));

  /* Precondition: not enough current space to satisfy nb request */
  assert ((unsigned long) (old_size) < (unsigned long) (nb + MINSIZE));


  if (av != &main_arena)
    {

      heap_info *old_heap, *heap;
      size_t old_heap_size;

      /* First try to extend the current heap. */
      old_heap = heap_for_ptr (old_top);
      old_heap_size = old_heap->size;
      if ((long) (MINSIZE + nb - old_size) > 0
          && grow_heap (old_heap, MINSIZE + nb - old_size) == 0)
        {
          av->system_mem += old_heap->size - old_heap_size;
          arena_mem += old_heap->size - old_heap_size;
          set_head (_md_old_top, (((char *) old_heap + old_heap->size) - (char *) old_top)
                    | PREV_INUSE);
        }
      else if ((heap = new_heap (nb + (MINSIZE + sizeof (*heap)), mp_.top_pad)))
        {
          /* Use a newly allocated heap.  */
          heap->ar_ptr = av;
          heap->prev = old_heap;
          av->system_mem += heap->size;
	  lookup_add_heap(heap, av->arena_index);
          arena_mem += heap->size;
          /* Set up the new top.  */
          topchunk = chunk_at_offset (heap, sizeof (*heap));
          av->_md_top = register_chunk(av, topchunk, false, 3);
	  /* fix the md_next pointer here; 
	   * the md_prev gets set after the fenceposts get put in.
	   */
	  av->_md_top->md_next = NULL;
	  
          set_head (av->_md_top, (heap->size - sizeof (*heap)) | PREV_INUSE);

          check_top(av);

          /* Setup fencepost and free the old top chunk with a multiple of
             MALLOC_ALIGNMENT in size. */
          /* The fencepost takes at least MINSIZE bytes, because it might
             become the top chunk again later.  Note that a footer is set
             up, too, although the chunk is marked in use. */
          old_size = (old_size - MINSIZE) & ~MALLOC_ALIGN_MASK;

          fencepost_0 = chunk_at_offset (old_top, old_size + 2 * SIZE_SZ);
          _md_fencepost_0 = register_chunk(av,  fencepost_0, false, 4);
	  _md_fencepost_0->md_prev = _md_old_top;
	  _md_fencepost_0->md_next = av->_md_top;
	  _md_old_top->md_next = _md_fencepost_0;
	  /* fix the new top's md_prev pointer */
	  av->_md_top->md_prev = _md_fencepost_0;
          set_head (_md_fencepost_0, 0 | PREV_INUSE);
	  check_metadata_chunk(av,fencepost_0,_md_fencepost_0);

          if (old_size >= MINSIZE)
            {
              fencepost_1 = chunk_at_offset (old_top, old_size);
	      _md_fencepost_1 = register_chunk(av,  fencepost_1, false, 5);

	      _md_fencepost_0->md_prev = _md_fencepost_1;
	      _md_fencepost_1->md_next = _md_fencepost_0;
	      _md_fencepost_1->md_prev = _md_old_top;

	      check_metadata_chunk(av,fencepost_0,_md_fencepost_0);
     
	      _md_old_top->md_next = _md_fencepost_1;

              set_head (_md_fencepost_1, (2 * SIZE_SZ) | PREV_INUSE);
              set_foot (av, _md_fencepost_1, fencepost_1, (2 * SIZE_SZ));
	      check_metadata_chunk(av,fencepost_1,_md_fencepost_1);

              set_head (_md_old_top, old_size | PREV_INUSE);
	      check_metadata_chunk(av, old_top,_md_old_top);

              _int_free (av, _md_old_top, old_top, true);
	      check_metadata_chunk(av, topchunk,av->_md_top);

            }
          else
            {
              set_head (_md_old_top, (old_size + 2 * SIZE_SZ) | PREV_INUSE);
              set_foot (av, _md_old_top, old_top, (old_size + 2 * SIZE_SZ));
	      check_metadata_chunk(av, old_top,_md_old_top);

            }
	  
	  check_top(av);
        }
      else if (!tried_mmap)
        /* We can at least try to use to mmap memory.  */
        goto try_mmap;
    }
  else     
    { /* av == main_arena */

      /* Request enough space for nb + pad + overhead */
      size = nb + mp_.top_pad + MINSIZE;

      /*
        If contiguous, we can subtract out existing space that we hope to
        combine with new space. We add it back later only if
        we don't actually get contiguous space.
      */

      if (contiguous (av))
        size -= old_size;

      /*
        Round to a multiple of page size.
        If MORECORE is not contiguous, this ensures that we only call it
        with whole-page arguments.  And if MORECORE is contiguous and
        this is not first time through, this preserves page-alignment of
        previous calls. Otherwise, we correct to page-align below.
      */

      size = ALIGN_UP (size, pagesize);

      /*
        Don't try to call MORECORE if argument is so big as to appear
        negative. Note that since mmap takes size_t arg, it may succeed
        below even if we cannot call MORECORE.
      */

      if (size > 0)
        {
          brk = (char *) (MORECORE (size));
          LIBC_PROBE (memory_sbrk_more, 2, brk, size);
        }

      if (brk != (char *) (MORECORE_FAILURE))
        {
          /* Call the `morecore' hook if necessary.  */
          void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
          if (__builtin_expect (hook != NULL, 0))
            (*hook)();
        }
      else
        {
          /*
            If have mmap, try using it as a backup when MORECORE fails or
            cannot be used. This is worth doing on systems that have "holes" in
            address space, so sbrk cannot extend to give contiguous space, but
            space is available elsewhere.  Note that we ignore mmap max count
            and threshold limits, since the space will not be used as a
            segregated mmap region.
          */

          /* Cannot merge with old top, so add its size back in */
          if (contiguous (av))
            size = ALIGN_UP (size + old_size, pagesize);

          /* If we are relying on mmap as backup, then use larger units */
          if ((unsigned long) (size) < (unsigned long) (MMAP_AS_MORECORE_SIZE))
            size = MMAP_AS_MORECORE_SIZE;

          /* Don't try if size wraps around 0 */
          if ((unsigned long) (size) > (unsigned long) (nb))
            {
              
	      mbrk = (char *) (sys_MMAP (0, size, PROT_READ | PROT_WRITE, 0));

              if (mbrk != MAP_FAILED)
                {
                  /* We do not need, and cannot use, another sbrk call to find end */
                  brk = mbrk;
                  snd_brk = brk + size;

                  /*
                    Record that we no longer have a contiguous sbrk region.
                    After the first time mmap is used as backup, we do not
                    ever rely on contiguous space since this could incorrectly
                    bridge regions.
                  */
                  set_noncontiguous (av);
                }
            }
        }

      if (brk != (char *) (MORECORE_FAILURE))
        {
	  
	  if(mbrk != NULL){

	    lookup_add_sbrk_region(brk, snd_brk);

	  } else {

	    if(mp_.sbrk_base == 0){
	      lookup_set_sbrk_lo( brk );
	    }

	    lookup_incr_sbrk_hi( size );

	  }

          if (mp_.sbrk_base == 0){
            mp_.sbrk_base = brk;
	  }
	  

          av->system_mem += size;

          /*
            If MORECORE extends previous space, we can likewise extend top size.
          */

          if (brk == old_end && snd_brk == (char *) (MORECORE_FAILURE)) {
            set_head (_md_old_top, (size + old_size) | PREV_INUSE);
          }
          else if (contiguous (av) && old_size && brk < old_end)
            {
              /* Oops!  Someone else killed our space..  Can't touch anything.  */
              malloc_printerr (3, "break adjusted to free malloc space", brk,
                               av);
            }

          /*
            Otherwise, make adjustments:

            * If the first time through or noncontiguous, we need to call sbrk
            just to find out where the end of memory lies.

            * We need to ensure that all returned chunks from malloc will meet
            MALLOC_ALIGNMENT

            * If there was an intervening foreign sbrk, we need to adjust sbrk
            request size to account for fact that we will not be able to
            combine new space with existing space in old_top.

            * Almost all systems internally allocate whole pages at a time, in
            which case we might as well use the whole last page of request.
            So we allocate enough more memory to hit a page boundary now,
            which in turn causes future contiguous calls to page-align.
          */

          else
            {
              front_misalign = 0;
              end_misalign = 0;
              correction = 0;
              aligned_brk = brk;

              /* handle contiguous cases */
              if (contiguous (av))
                {
                  /* Count foreign sbrk as system_mem.  */
                  if (old_size)
                    av->system_mem += brk - old_end;

                  /* Guarantee alignment of first new chunk made from this space */

                  front_misalign = (INTERNAL_SIZE_T) chunk2mem (brk) & MALLOC_ALIGN_MASK;
                  if (front_misalign > 0)
                    {
                      /*
                        Skip over some bytes to arrive at an aligned position.
                        We don't need to specially mark these wasted front bytes.
                        They will never be accessed anyway because
                        prev_inuse of av->_md_top (and any chunk created from its start)
                        is always true after initialization.
                      */

                      correction = MALLOC_ALIGNMENT - front_misalign;
                      aligned_brk += correction;
                    }

                  /*
                    If this isn't adjacent to existing space, then we will not
                    be able to merge with old_top space, so must add to 2nd request.
                  */

                  correction += old_size;

                  /* Extend the end address to hit a page boundary */
                  end_misalign = (INTERNAL_SIZE_T) (brk + size + correction);
                  correction += (ALIGN_UP (end_misalign, pagesize)) - end_misalign;

                  assert (correction >= 0);
                  snd_brk = (char *) (MORECORE (correction));

                  /*
                    If can't allocate correction, try to at least find out current
                    brk.  It might be enough to proceed without failing.

                    Note that if second sbrk did NOT fail, we assume that space
                    is contiguous with first sbrk. This is a safe assumption unless
                    program is multithreaded but doesn't use locks and a foreign sbrk
                    occurred between our first and second calls.
                  */

                  if (snd_brk == (char *) (MORECORE_FAILURE))
                    {
                      correction = 0;
                      snd_brk = (char *) (MORECORE (0));
                    }
                  else
                    {
                      /* Call the `morecore' hook if necessary.  */
                      void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
                      if (__builtin_expect (hook != NULL, 0))
                        (*hook)();
                    }
                }

              /* handle non-contiguous cases */
              else
                {
                  if (MALLOC_ALIGNMENT == 2 * SIZE_SZ)
                    /* MORECORE/mmap must correctly align */
                    assert (((unsigned long) chunk2mem (brk) & MALLOC_ALIGN_MASK) == 0);
                  else
                    {
                      front_misalign = (INTERNAL_SIZE_T) chunk2mem (brk) & MALLOC_ALIGN_MASK;
                      if (front_misalign > 0)
                        {
                          /*
                            Skip over some bytes to arrive at an aligned position.
                            We don't need to specially mark these wasted front bytes.
                            They will never be accessed anyway because
                            prev_inuse of av->_md_top (and any chunk created from its start)
                            is always true after initialization.
                          */

                          aligned_brk += MALLOC_ALIGNMENT - front_misalign;
                        }
                    }

                  /* Find out current end of memory */
                  if (snd_brk == (char *) (MORECORE_FAILURE))
                    {
                      snd_brk = (char *) (MORECORE (0));
                    }
                }

              /* Adjust top based on results of second sbrk */
              if (snd_brk != (char *) (MORECORE_FAILURE))
                {
		  
                  topchunk = (mchunkptr) aligned_brk;
                  av->_md_top = register_chunk(av, topchunk, false, 6);
		  av->_md_top->md_next = NULL;
		  av->_md_top->md_prev = NULL;
		  /* we refix the md_prev pointer once the fenceposts have been put it */

                  set_head (av->_md_top, (snd_brk - aligned_brk + correction) | PREV_INUSE);
                  check_top(av);

                  av->system_mem += correction;
		  
		  if(correction > 0){
		    lookup_incr_sbrk_hi(correction);
		  }

                  /*
                    If not the first time through, we either have a
                    gap due to foreign sbrk or a non-contiguous region.  Insert a
                    double fencepost at old_top to prevent consolidation with space
                    we don't own. These fenceposts are artificial chunks that are
                    marked as inuse and are in any case too small to use.  We need
                    two to make sizes and alignments work out.
                  */

                  if (old_size != 0)
                    {
                      /*
                        Shrink old_top to insert fenceposts, keeping size a
                        multiple of MALLOC_ALIGNMENT. We know there is at least
                        enough space in old_top to do this.
                      */
                      old_size = (old_size - 4 * SIZE_SZ) & ~MALLOC_ALIGN_MASK;
                      set_head (_md_old_top, old_size | PREV_INUSE);
                      /*
                        Note that the following assignments completely overwrite
                        old_top when old_size was previously MINSIZE.  This is
                        intentional. We need the fencepost, even if old_top otherwise gets
                        lost.
                      */
                      fencepost_0 = chunk_at_offset (old_top, old_size);
		      _md_fencepost_0 = register_chunk(av, fencepost_0, false, 7);

		      _md_fencepost_0->md_next = _md_old_top->md_next;
		      _md_fencepost_0->md_prev = _md_old_top;
		      _md_old_top->md_next = _md_fencepost_0;

                      set_head(_md_fencepost_0, (2 * SIZE_SZ) | PREV_INUSE); 
                                            
                      fencepost_1 = chunk_at_offset (old_top, old_size + 2 * SIZE_SZ);
                      _md_fencepost_1 = register_chunk(av, fencepost_1, false, 8);

		      _md_fencepost_1->md_next = _md_fencepost_0->md_next;
		      _md_fencepost_1->md_prev = _md_fencepost_0;
		      _md_fencepost_0->md_next = _md_fencepost_1;

		      av->_md_top->md_prev = _md_fencepost_1;

                      set_head(_md_fencepost_1, (2 * SIZE_SZ) | PREV_INUSE);
                      
		      check_metadata_chunk(av, fencepost_0, _md_fencepost_0);
		      check_metadata_chunk(av, fencepost_1, _md_fencepost_1);
		      check_metadata_chunk(av, old_top, _md_old_top);
		      check_top(av);

                      /* If possible, release the rest. */
                      if (old_size >= MINSIZE)
                        {
                          _int_free (av, _md_old_top, old_top, true);
                        }
                    }
                }
            }
        }
    } /* (av ==  &main_arena) */

  if ((unsigned long) av->system_mem > (unsigned long) (av->max_system_mem))
    av->max_system_mem = av->system_mem;
  check_malloc_state (av);

  /* finally, do the allocation */
  _md_p = av->_md_top;
  p = chunkinfo2chunk(_md_p);
  size = chunksize (_md_p);

  /* check that one of the above allocation paths succeeded */
  if ((unsigned long) (size) >= (unsigned long) (nb + MINSIZE))
    {
      av->_md_top = split_chunk(av, _md_p, p, size, nb);  
      check_top(av);
      check_malloced_chunk(av, p, _md_p, nb);
      return _md_p;
    }

  /* catch all failure paths */
  __set_errno (ENOMEM);
  return 0;
}


/*
  systrim is an inverse of sorts to sysmalloc.  It gives memory back
  to the system (via negative arguments to sbrk) if there is unused
  memory at the `high' end of the malloc pool. It is called
  automatically by free() when top space exceeds the trim
  threshold. It is also called by the public malloc_trim routine.  It
  returns 1 if it actually released any memory, else 0.
*/

static int
systrim (size_t pad, mstate av)
{
  long top_size;         /* Amount of top-most memory */
  long extra;            /* Amount to release */
  long released;         /* Amount actually released */
  char *current_brk;     /* address returned by pre-check sbrk call */
  char *new_brk;         /* address returned by post-check sbrk call */
  size_t pagesize;
  long top_area;
  mchunkptr topchunk;

  pagesize = GLRO (dl_pagesize);
  top_size = chunksize (av->_md_top);

  top_area = top_size - MINSIZE - 1;
  if (top_area <= pad)
    return 0;

  /* Release in pagesize units and round down to the nearest page.  */
  extra = ALIGN_DOWN(top_area - pad, pagesize);

  if (extra == 0)
    return 0;

  /*
    Only proceed if end of memory is where we last set it.
    This avoids problems if there were foreign sbrk calls.
  */
  current_brk = (char *) (MORECORE (0));
  topchunk = chunkinfo2chunk(av->_md_top);
  if (current_brk == (char *) (topchunk) + top_size)
    {
      /*
        Attempt to release memory. We ignore MORECORE return value,
        and instead call again to find out where new end of memory is.
        This avoids problems if first call releases less than we asked,
        of if failure somehow altered brk value. (We could still
        encounter problems if it altered brk in some very bad way,
        but the only thing we can do is adjust anyway, which will cause
        some downstream failure.)
      */

      MORECORE (-extra);
      /* Call the `morecore' hook if necessary.  */
      void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
      if (__builtin_expect (hook != NULL, 0))
        (*hook)();
      new_brk = (char *) (MORECORE (0));

      LIBC_PROBE (memory_sbrk_less, 2, new_brk, extra);

      if (new_brk != (char *) MORECORE_FAILURE)
        {
          released = (long) (current_brk - new_brk);

          if (released != 0)
            {
              /* Success. Adjust top. */
              av->system_mem -= released;
              set_head (av->_md_top, (top_size - released) | PREV_INUSE);
              check_malloc_state (av);

	      if(av == &main_arena){
		lookup_decr_sbrk_hi(released);
	      }

              return 1;
            }
        }
    }
  return 0;
}

static void
internal_function
munmap_chunk (chunkinfoptr _md_p)
{
  INTERNAL_SIZE_T size = chunksize (_md_p);
  mchunkptr p = chunkinfo2chunk(_md_p);

  assert (chunk_is_mmapped (p));

  uintptr_t block = (uintptr_t) p - _md_p->prev_size;
  size_t total_size = _md_p->prev_size + size;
  /* Unfortunately we have to do the compilers job by hand here.  Normally
     we would test BLOCK and TOTAL-SIZE separately for compliance with the
     page size.  But gcc does not recognize the optimization possibility
     (in the moment at least) so we combine the two values into one before
     the bit test.  */
  if (__builtin_expect (((block | total_size) & (GLRO (dl_pagesize) - 1)) != 0, 0))
    {
      malloc_printerr (check_action, "munmap_chunk(): invalid pointer",
                       chunk2mem (p), NULL);
      return;
    }

  atomic_decrement (&mp_.n_mmaps);
  atomic_add (&mp_.mmapped_mem, -total_size);

  lookup_delete_mmap(p);

  /* If munmap failed the process virtual memory address space is in a
     bad shape.  Just leave the block hanging around, the process will
     terminate shortly anyway since not much can be done.  */
  __munmap ((char *) block, total_size);
}

#if HAVE_MREMAP

static mchunkptr
internal_function
mremap_chunk (mstate av, chunkinfoptr _md_p, size_t new_size)
{
  size_t pagesize;
  INTERNAL_SIZE_T offset, size;
  char *cp, *ocp;
  mchunkptr p, op;

  pagesize = GLRO (dl_pagesize);
  size = chunksize (_md_p);
  p = chunkinfo2chunk(_md_p);
  offset = _md_p->prev_size;

  assert (chunk_is_mmapped (p));
  assert (((size + offset) & (GLRO (dl_pagesize) - 1)) == 0);

  /* remember for later */
  op = p;

  /* Note the extra SIZE_SZ overhead as in mmap_chunk(). */
  new_size = ALIGN_UP (new_size + offset + SIZE_SZ, pagesize);

  /* No need to remap if the number of pages does not change.  */
  if (size + offset == new_size)
    return p;

  ocp = (char *) p - offset;

  cp = (char *) __mremap (ocp, size + offset, new_size, MREMAP_MAYMOVE);

  if (cp == MAP_FAILED)
    return 0;

  p = (mchunkptr) (cp + offset);

  assert (aligned_OK ((unsigned long)chunk2mem (p)));


  if (p != op) {
    /* remove the old one */
    unregister_chunk(av, op, false);
    // Done: _md_p->md_prev = _md_p->md_next = NULL;
    lookup_delete_mmap(op);
    lookup_add_mmap(p, new_size);
    _md_p = register_chunk(av, p, true, 9);
    check_metadata_chunk(av, p, _md_p);
    _md_p->prev_size = offset;
  }

  assert ((_md_p->prev_size == offset));
  
  set_head (_md_p, (new_size - offset));


  INTERNAL_SIZE_T new;
  new = atomic_exchange_and_add (&mp_.mmapped_mem, new_size - size - offset)
    + new_size - size - offset;
  atomic_max (&mp_.max_mmapped_mem, new);
  return p;
}
#endif /* HAVE_MREMAP */

/*------------------------ Public wrappers. --------------------------------*/

void *
__libc_malloc (size_t bytes)
{
  mstate ar_ptr;
  void *mem;
  chunkinfoptr _md_victim;
  
    
  void *(*hook) (size_t, const void *)
    = atomic_forced_read (__malloc_hook);
  if (__builtin_expect (hook != NULL, 0))
    return (*hook)(bytes, RETURN_ADDRESS (0));

  arena_get (ar_ptr, bytes, MALLOC_SITE);

  _md_victim = _int_malloc (ar_ptr, bytes);
  /* Retry with another arena only if we were able to find a usable arena
     before.  */
  if (!_md_victim && ar_ptr != NULL)
    {
      LIBC_PROBE (memory_malloc_retry, 1, bytes);
      ar_ptr = arena_get_retry (ar_ptr, bytes, MALLOC_SITE);
      _md_victim = _int_malloc (ar_ptr, bytes);
    }

  if (ar_ptr != NULL)
    UNLOCK_ARENA(ar_ptr, MALLOC_SITE);

  mem = chunkinfo2mem(_md_victim);

  assert (!mem || chunk_is_mmapped (mem2chunk (mem)) ||
          ar_ptr == arena_for_chunk (mem2chunk (mem)));
  
  if (ar_ptr != NULL)check_top(ar_ptr);

  return mem;
}
libc_hidden_def (__libc_malloc)

void
__libc_free (void *mem)
{
  mstate ar_ptr;
  mchunkptr p;                          /* chunk corresponding to mem */
  chunkinfoptr _md_p;                   /* metadata of chunk corresponding to mem */
  bool sane;

  void (*hook) (void *, const void *)
    = atomic_forced_read (__free_hook);
  if (__builtin_expect (hook != NULL, 0))
    {
      (*hook)(mem, RETURN_ADDRESS (0));
      return;
    }

  if (mem == 0)                              /* free(0) has no effect */
    return;

  p = mem2chunk (mem);

  /* proceed only if sane */
  sane = arena_is_sane(p);
  assert(sane);
  if (!sane) { 
    fprintf(stderr, "insane free: arena_index = %zu\n", p->arena_index);
    abort();
    return; 
  }

  ar_ptr = arena_from_chunk (p);

  size_t index = 0;
  bool success = lookup_arena_index(p, &index);
  if(!success){
    fprintf(stderr, 
	    "lookup_arena_index(%p) failed for the arena: %zu  p->arena_index = %zu\n", 
	    p, ar_ptr->arena_index, p->arena_index);
    lookup_dump(stderr);
  }
  assert(success);
  
  if (chunk_is_mmapped (p))                       /* release mmapped memory. */
    {
      LOCK_ARENA(ar_ptr, FREE_SITE);
      
      _md_p = lookup_chunk(ar_ptr, p);  
  
      if (_md_p == NULL) { 
        missing_metadata(ar_ptr, p);
      }


      /* see if the dynamic brk/mmap threshold needs adjusting */
      if (!mp_.no_dyn_threshold
          && _md_p->size > mp_.mmap_threshold
          && _md_p->size <= DEFAULT_MMAP_THRESHOLD_MAX)
        {
          mp_.mmap_threshold =  chunksize (_md_p);
          mp_.trim_threshold = 2 * mp_.mmap_threshold;
          LIBC_PROBE (memory_mallopt_free_dyn_thresholds, 2,
                      mp_.mmap_threshold, mp_.trim_threshold);
        }
      

      munmap_chunk (_md_p); 

      unregister_chunk(ar_ptr, p, false);
      
      UNLOCK_ARENA(ar_ptr, FREE_SITE);
      return;
    }


  _int_free (ar_ptr, NULL, p, false);

  check_top(ar_ptr);

}
libc_hidden_def (__libc_free)

void *
__libc_realloc (void *oldmem, size_t bytes)
{
  mstate ar_ptr;
  INTERNAL_SIZE_T nb;         /* padded request size */
  chunkinfoptr _md_oldp;      /* metadata of oldp */

  void *mem;                  /* mem to return  */
  mchunkptr newp;             /* chunk of mem to return  */
  chunkinfoptr _md_newp;      /* metadata of mem */

  bool sane;                  /* arena_index is sane */

  void *(*hook) (void *, size_t, const void *) =
    atomic_forced_read (__realloc_hook);
  if (__builtin_expect (hook != NULL, 0))
    return (*hook)(oldmem, bytes, RETURN_ADDRESS (0));

#if REALLOC_ZERO_BYTES_FREES
  if (bytes == 0 && oldmem != NULL)
    {
      __libc_free (oldmem); return 0;
    }
#endif

  /* realloc of null is supposed to be same as malloc */
  if (oldmem == 0)
    return __libc_malloc (bytes);

  /* chunk corresponding to oldmem */
  const mchunkptr oldp = mem2chunk (oldmem);


  /* proceed only if sane */
  sane = arena_is_sane(oldp);
  assert(sane);
  if (!sane) { 
    fprintf(stderr, "insane realloc: arena_index = %zu\n", oldp->arena_index);
    abort();
    return 0; 
  }

  ar_ptr = arena_from_chunk (oldp);

  size_t index = 0;
  bool success = lookup_arena_index(oldp, &index);
  if(!success){
    fprintf(stderr, 
	    "lookup_arena_index(%p) failed for the arena: %zu  p->arena_index = %zu\n", 
	    oldp, ar_ptr->arena_index, oldp->arena_index);
    lookup_dump(stderr);
  }
  assert(success);

  LOCK_ARENA(ar_ptr, REALLOC_SITE);

  _md_oldp = lookup_chunk(ar_ptr, oldp);
  if (_md_oldp == NULL) { 
    missing_metadata(ar_ptr, oldp); 
    UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
    return 0;
  }


  /* gracefully fail is we do not have enough memory to 
     replenish our metadata cache */
  if (ar_ptr != NULL && !replenish_metadata_cache(ar_ptr)) {
    UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
    return 0;
  }   

  /* its size */
  const INTERNAL_SIZE_T oldsize = chunksize (_md_oldp);


  /* Little security check which won't hurt performance: the
     allocator never wraps around at the end of the address space.
     Therefore we can exclude some size values which might appear
     here by accident or by "design" from some intruder.  */
  if (__builtin_expect ((uintptr_t) oldp > (uintptr_t) -oldsize, 0)
      || __builtin_expect (misaligned_chunk (oldp), 0))
    {
      malloc_printerr (check_action, "realloc(): invalid pointer", oldmem,
                       ar_ptr);
      UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
      return 0;
    }

  if ( !checked_request2size (bytes, &nb) ) {
    UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
    return 0;
  }

  if (chunk_is_mmapped (oldp))
    {
      void *newmem;
#if HAVE_MREMAP
      if(ar_ptr != &main_arena){
	UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
	LOCK_ARENA(&main_arena, REALLOC_SITE);
      }
      newp = mremap_chunk (&main_arena, _md_oldp, nb);
      if (newp) {
	if(ar_ptr == &main_arena){
	  UNLOCK_ARENA(&main_arena, REALLOC_SITE);
	}
        return chunk2mem (newp);
      }
      if(ar_ptr != &main_arena){
	UNLOCK_ARENA(&main_arena, REALLOC_SITE);
	LOCK_ARENA(ar_ptr, REALLOC_SITE);
      }
#endif
      /* Note the extra SIZE_SZ overhead. */
      if (oldsize - SIZE_SZ >= nb){
	UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
        return oldmem;                         /* do nothing */
      }
      /* Must alloc, copy, free. */
      UNLOCK_ARENA(ar_ptr, REALLOC_SITE);
      newmem = __libc_malloc (bytes);
      if (newmem == 0){
        return 0;              /* propagate failure */
      }
      memcpy (newmem, oldmem, oldsize - 2 * SIZE_SZ);
      munmap_chunk (_md_oldp);
      LOCK_ARENA(&main_arena, REALLOC_SITE);
      unregister_chunk(&main_arena, oldp, false);
      UNLOCK_ARENA(&main_arena, REALLOC_SITE);
      return newmem;
    }


  _md_newp = _int_realloc (ar_ptr, _md_oldp, oldsize, nb);
  newp = chunkinfo2chunk(_md_newp);
  mem = chunkinfo2mem(_md_newp);

  UNLOCK_ARENA(ar_ptr, REALLOC_SITE);

  assert (!mem || chunk_is_mmapped (newp) ||
          ar_ptr == arena_for_chunk (newp));

  if (mem == NULL)
    {
      /* Try harder to allocate memory in other arenas.  */
      LIBC_PROBE (memory_realloc_retry, 2, bytes, oldmem);
      mem = __libc_malloc (bytes);
      if (mem != NULL)
        {
          memcpy (mem, oldmem, oldsize - SIZE_SZ);
          _int_free (ar_ptr,_md_oldp, oldp, false);
        }
    }

  if (ar_ptr != NULL) check_top(ar_ptr);

  return mem;
}

libc_hidden_def (__libc_realloc)

void *
__libc_memalign (size_t alignment, size_t bytes)
{
  void *address = RETURN_ADDRESS (0);
  return _mid_memalign (alignment, bytes, address);
}

static void *
_mid_memalign (size_t alignment, size_t bytes, void *address)
{
  mstate ar_ptr;
  void *p;
  chunkinfoptr _md_p;

  void *(*hook) (size_t, size_t, const void *) =
    atomic_forced_read (__memalign_hook);
  if (__builtin_expect (hook != NULL, 0))
    return (*hook)(alignment, bytes, address);

  /* If we need less alignment than we give anyway, just relay to malloc.  */
  if (alignment <= MALLOC_ALIGNMENT)
    return __libc_malloc (bytes);

  /* Otherwise, ensure that it is at least a minimum chunk size */
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

  arena_get (ar_ptr, bytes + alignment + MINSIZE, MEMALIGN_SITE);

  /* gracefully fail is we do not have enough memory to 
     replenish our metadata cache */
  if (ar_ptr != NULL && !replenish_metadata_cache(ar_ptr)) {
    UNLOCK_ARENA(ar_ptr, MEMALIGN_SITE);
    return 0;
  }  

  _md_p = _int_memalign (ar_ptr, alignment, bytes);
  if (!_md_p && ar_ptr != NULL)
    {
      LIBC_PROBE (memory_memalign_retry, 2, bytes, alignment);
      ar_ptr = arena_get_retry (ar_ptr, bytes, MEMALIGN_SITE);
      _md_p = _int_memalign (ar_ptr, alignment, bytes);
    }

  if (ar_ptr != NULL)
    UNLOCK_ARENA(ar_ptr, MEMALIGN_SITE);

  p = chunkinfo2mem(_md_p);

  assert (!p || chunk_is_mmapped (mem2chunk (p)) ||
          ar_ptr == arena_for_chunk (mem2chunk (p)));
  return p;
}
/* For ISO C11.  */
weak_alias (__libc_memalign, aligned_alloc)
libc_hidden_def (__libc_memalign)

void *
__libc_valloc (size_t bytes)
{
  if (__malloc_initialized < 0)
    ptmalloc_init ();

  void *address = RETURN_ADDRESS (0);
  size_t pagesize = GLRO (dl_pagesize);
  return _mid_memalign (pagesize, bytes, address);
}

void *
__libc_pvalloc (size_t bytes)
{
  if (__malloc_initialized < 0)
    ptmalloc_init ();

  void *address = RETURN_ADDRESS (0);
  size_t pagesize = GLRO (dl_pagesize);
  size_t rounded_bytes = ALIGN_UP (bytes, pagesize);

  /* Check for overflow.  */
  if (bytes > SIZE_MAX - 2 * pagesize - MINSIZE)
    {
      __set_errno (ENOMEM);
      return 0;
    }

  return _mid_memalign (pagesize, rounded_bytes, address);
}

void *
__libc_calloc (size_t n, size_t elem_size)
{
  mstate av;
  mchunkptr oldtop;
  chunkinfoptr _md_oldtop;
  INTERNAL_SIZE_T bytes, sz, csz, oldtopsize;

  void *mem;                /* memory from malloc */
  mchunkptr victim;         /* chunk of memory from malloc */
  chunkinfoptr _md_victim;  /* metadata of memory from malloc */

  unsigned long clearsize;
  unsigned long nclears;
  INTERNAL_SIZE_T *d;

  /* size_t is unsigned so the behavior on overflow is defined.  */
  bytes = n * elem_size;
#define HALF_INTERNAL_SIZE_T                                    \
  (((INTERNAL_SIZE_T) 1) << (8 * sizeof (INTERNAL_SIZE_T) / 2))
  if (__builtin_expect ((n | elem_size) >= HALF_INTERNAL_SIZE_T, 0))
    {
      if (elem_size != 0 && bytes / elem_size != n)
        {
          __set_errno (ENOMEM);
          return 0;
        }
    }

  void *(*hook) (size_t, const void *) =
    atomic_forced_read (__malloc_hook);
  if (__builtin_expect (hook != NULL, 0))
    {
      sz = bytes;
      mem = (*hook)(sz, RETURN_ADDRESS (0));
      if (mem == 0)
        return 0;

      return memset (mem, 0, sz);
    }

  sz = bytes;

  arena_get (av, sz, CALLOC_SITE);


  /* gracefully fail is we do not have enough memory to 
     replenish our metadata cache */
  if (av != NULL && !replenish_metadata_cache(av)) {
    UNLOCK_ARENA(av, CALLOC_SITE);
    return 0;
  }  

  if (av)
    {
      /* Check if we hand out the top chunk, in which case there may be no
         need to clear. */
#if MORECORE_CLEARS
      _md_oldtop = av->_md_top;
      oldtop = chunkinfo2chunk(_md_oldtop);
      oldtopsize = chunksize (_md_oldtop);
# if MORECORE_CLEARS < 2
      /* Only newly allocated memory is guaranteed to be cleared.  */
      if (av == &main_arena &&
          oldtopsize < mp_.sbrk_base + av->max_system_mem - (char *) oldtop)
        oldtopsize = (mp_.sbrk_base + av->max_system_mem - (char *) oldtop);
# endif
      if (av != &main_arena)
        {
          heap_info *heap = heap_for_ptr (oldtop);
          if (oldtopsize < (char *) heap + heap->mprotect_size - (char *) oldtop)
            oldtopsize = (char *) heap + heap->mprotect_size - (char *) oldtop;
        }
#endif
    }
  else
    {
      /* No usable arenas.  */
      oldtop = 0;
      _md_oldtop = 0;
      oldtopsize = 0;
    }
  _md_victim = _int_malloc (av, sz);
  victim = chunkinfo2chunk(_md_victim);
  mem = chunkinfo2mem(_md_victim);

  assert (!mem || chunk_is_mmapped (victim) ||
          av == arena_for_chunk (victim));

  if (mem == 0 && av != NULL)
    {
      LIBC_PROBE (memory_calloc_retry, 1, sz);
      av = arena_get_retry (av, sz, CALLOC_SITE);
      _md_victim = _int_malloc (av, sz);
      victim = chunkinfo2chunk(_md_victim);
      mem = chunkinfo2mem(_md_victim);
    }

  if (av != NULL)
    UNLOCK_ARENA(av, FREE_SITE);

  /* Allocation failed even after a retry.  */
  if (mem == 0)
    return 0;

  /* Two optional cases in which clearing not necessary */
  if (chunk_is_mmapped (victim))
    {
      if (__builtin_expect (perturb_byte, 0))
        return memset (mem, 0, sz);

      return mem;
    }

  csz = chunksize (_md_victim);

#if MORECORE_CLEARS
  if (perturb_byte == 0 && (victim == oldtop && csz > oldtopsize))
    {
      /* clear only the bytes from non-freshly-sbrked memory */
      csz = oldtopsize;
    }
#endif

  /* Unroll clear of <= 36 bytes (72 if 8byte sizes).  We know that
     contents have an odd number of INTERNAL_SIZE_T-sized words;
     minimally 3.  */
  d = (INTERNAL_SIZE_T *) mem;
  clearsize = csz - SIZE_SZ;
  nclears = clearsize / sizeof (INTERNAL_SIZE_T);
  assert (nclears >= 3);

  if (nclears > 9)
    return memset (d, 0, clearsize);

  else
    {
      *(d + 0) = 0;
      *(d + 1) = 0;
      *(d + 2) = 0;
      if (nclears > 4)
        {
          *(d + 3) = 0;
          *(d + 4) = 0;
          if (nclears > 6)
            {
              *(d + 5) = 0;
              *(d + 6) = 0;
              if (nclears > 8)
                {
                  *(d + 7) = 0;
                  *(d + 8) = 0;
                }
            }
        }
    }

  check_top(av);

  return mem;
}

/*
  ------------------------------ malloc ------------------------------
*/

static chunkinfoptr
_int_malloc (mstate av, size_t bytes)
{
  INTERNAL_SIZE_T nb;               /* normalized request size */
  unsigned int idx;                 /* associated bin index */
  mbinptr bin;                      /* associated bin */

  mchunkptr victim;                 /* inspected/selected chunk */
  chunkinfoptr _md_victim;          /* metadata of inspected/selected chunk */
  INTERNAL_SIZE_T size;             /* its size */
  int victim_index;                 /* its bin index */

  mchunkptr remainder;              /* remainder from a split */
  chunkinfoptr _md_remainder;       /* metadata of remainder from a split */
  chunkinfoptr _md_temp;            /* temporary handle of metadata */
  unsigned long remainder_size;     /* its size */

  unsigned int block;               /* bit map traverser */
  unsigned int bit;                 /* bit map traverser */
  unsigned int map;                 /* current word of binmap */

  chunkinfoptr fwd;                    /* misc temp for linking */
  chunkinfoptr bck;                    /* misc temp for linking */


  chunkinfoptr _md_p;               /* result of sysmalloc */
  void* mem;                        /* mem of _md_p        */

  const char *errstr = NULL;

  /*
    Convert request size to internal form by adding SIZE_SZ bytes
    overhead plus possibly more to obtain necessary alignment and/or
    to obtain a size of at least MINSIZE, the smallest allocatable
    size. Also, checked_request2size traps (returning 0) request sizes
    that are so large that they wrap around zero when padded and
    aligned.
  */

  if ( !checked_request2size (bytes, &nb) ) {
    return 0;
  }

  /* There are no usable arenas.  Fall back to sysmalloc to get a chunk from
     mmap.  */
  if (__glibc_unlikely (av == NULL))
    {
      _md_p = sysmalloc (nb, av);
      mem = chunkinfo2mem(_md_p);
      if (mem != NULL)
        alloc_perturb (mem, bytes);
      return _md_p;
    }

  /* gracefully fail is we do not have enough memory to 
     replenish our metadata cache
  */
  if (av != NULL && !replenish_metadata_cache(av)) 
    {
      return 0;
    } 
  



  /*
    If the size qualifies as a fastbin, first check corresponding bin.
    This code is safe to execute even if av is not yet initialized, so we
    can try it without checking, which saves some time on this fast path.
  */

  if ((unsigned long) (nb) <= (unsigned long) (get_max_fast ()))
    {
      idx = fastbin_index (nb);
      mfastbinptr *fb = &fastbin (av, idx);
      chunkinfoptr pp = *fb;
      do
        {
          _md_victim = pp;
          if (_md_victim == NULL)
            break;
        }
      while ((pp = catomic_compare_and_exchange_val_acq (fb, _md_victim->fd, _md_victim))
             != _md_victim);
      if (_md_victim != 0)
        {
          if (__builtin_expect (fastbin_index (chunksize (_md_victim)) != idx, 0))
            {
              errstr = "malloc(): memory corruption (fast)";
            errout:
              malloc_printerr (check_action, errstr, chunkinfo2mem (_md_victim), av);
              return 0;
            }
	  victim = chunkinfo2chunk(_md_victim);
          check_remalloced_chunk (av, victim, _md_victim, nb);
          void *p = chunk2mem (victim);
          alloc_perturb (p, bytes);
          return _md_victim;
        }
    }

  /*
    If a small request, check regular bin.  Since these "smallbins"
    hold one size each, no searching within bins is necessary.
    (For a large request, we need to wait until unsorted chunks are
    processed to find best fit. But for small ones, fits are exact
    anyway, so we can check now, which is faster.)
  */

  if (in_smallbin_range (nb))
    {
      idx = smallbin_index (nb);
      bin = bin_at (av, idx);
      
      if ((_md_victim = last (bin)) != bin)
        {
          if (_md_victim == 0) /* initialization check */
            malloc_consolidate (av);
          else
            {
              bck = _md_victim->bk;
              if (__glibc_unlikely (bck->fd != _md_victim))
                {
                  errstr = "malloc(): smallbin double linked list corrupted";
                  goto errout;
                }
	      victim = chunkinfo2chunk(_md_victim);
              set_inuse_bit_at_offset (av, _md_victim, victim, nb);
              bin->bk = bck;
              bck->fd = bin;
              
              check_malloced_chunk (av, victim, _md_victim, nb);
              void *p = chunk2mem (victim);
              alloc_perturb (p, bytes);
              return _md_victim;
            }
        }
    }

  /*
    If this is a large request, consolidate fastbins before continuing.
    While it might look excessive to kill all fastbins before
    even seeing if there is space available, this avoids
    fragmentation problems normally associated with fastbins.
    Also, in practice, programs tend to have runs of either small or
    large requests, but less often mixtures, so consolidation is not
    invoked all that often in most programs. And the programs that
    it is called frequently in otherwise tend to fragment.
  */

  else
    {
      idx = largebin_index (nb);
      if (have_fastchunks (av))
        malloc_consolidate (av);
    }

  /*
    Process recently freed or remaindered chunks, taking one only if
    it is exact fit, or, if this a small request, the chunk is remainder from
    the most recent non-exact fit.  Place other traversed chunks in
    bins.  Note that this step is the only place in any routine where
    chunks are placed in bins.

    The outer loop here is needed because we might not realize until
    near the end of malloc that we should have consolidated, so must
    do so and retry. This happens at most once, and only when we would
    otherwise need to expand memory to service a "small" request.
  */

  for (;; )
    {
      int iters = 0;
      while ((_md_victim = unsorted_chunks (av)->bk) != unsorted_chunks (av))
        {

          bck = _md_victim->bk;
	  victim = chunkinfo2chunk(_md_victim);

          if (__builtin_expect (_md_victim->size <= 2 * SIZE_SZ, 0)
              || __builtin_expect (_md_victim->size > av->system_mem, 0))
            malloc_printerr (check_action, "malloc(): memory corruption",
                             chunk2mem (victim), av);
          size = chunksize (_md_victim);

          /*
            If a small request, try to use last remainder if it is the
            only chunk in unsorted bin.  This helps promote locality for
            runs of consecutive small requests. This is the only
            exception to best-fit, and applies only when there is
            no exact fit for a small chunk.
          */

          if (in_smallbin_range (nb) &&
              bck == unsorted_chunks (av) &&
              _md_victim == av->last_remainder &&
              (unsigned long) (size) > (unsigned long) (nb + MINSIZE))
            {
              /* split and reattach remainder */
              remainder_size = size - nb;
              remainder = chunk_at_offset (victim, nb);
              set_head (_md_victim, nb | PREV_INUSE);

              _md_remainder = register_chunk(av, remainder, false, 10);

	      _md_temp = _md_victim->md_next;

	      _md_remainder->md_next = _md_temp;
	      _md_remainder->md_prev = _md_victim;
	      _md_victim->md_next = _md_remainder;

	      if(_md_temp != NULL){
		_md_temp->md_prev = _md_remainder;
		check_metadata(av, _md_temp);
	      }

              set_head (_md_remainder, remainder_size | PREV_INUSE);
              set_foot (av, _md_remainder, remainder, remainder_size);

	      check_metadata_chunk(av, remainder, _md_remainder);
	      check_metadata_chunk(av, victim, _md_victim);
	      	      

              unsorted_chunks (av)->bk = unsorted_chunks (av)->fd = _md_remainder;
              av->last_remainder = _md_remainder;
              _md_remainder->bk = _md_remainder->fd = unsorted_chunks (av);
              if (!in_smallbin_range (remainder_size))
                {
                  _md_remainder->fd_nextsize = NULL;
                  _md_remainder->bk_nextsize = NULL;
                }

              check_malloced_chunk (av, victim, _md_victim, nb);
              void *p = chunk2mem (victim);
              alloc_perturb (p, bytes);
              return _md_victim;
            }

          /* remove from unsorted list */
          unsorted_chunks (av)->bk = bck;
          bck->fd = unsorted_chunks (av);

          /* Take now instead of binning if exact fit */

          if (size == nb)
            {
              set_inuse_bit_at_offset (av, _md_victim, victim, size);
              check_malloced_chunk (av, victim, _md_victim, nb);
              void *p = chunk2mem (victim);
              alloc_perturb (p, bytes);
              return _md_victim;
            }

          /* place chunk in bin */

          if (in_smallbin_range (size))
            {
              victim_index = smallbin_index (size);
              bck = bin_at (av, victim_index);
              fwd = bck->fd;
            }
          else
            {
              victim_index = largebin_index (size);
              bck = bin_at (av, victim_index);
              fwd = bck->fd;

              /* maintain large bins in sorted order */
              if (fwd != bck)
                {
                  /* Or with inuse bit to speed comparisons */
                  size |= PREV_INUSE;
                  /* if smaller than smallest, bypass loop below */
                  if ((unsigned long) (size) < (unsigned long) (bck->bk->size))
                    {
                      fwd = bck;
                      bck = bck->bk;

                      _md_victim->fd_nextsize = fwd->fd;
                      _md_victim->bk_nextsize = fwd->fd->bk_nextsize;
                      fwd->fd->bk_nextsize = _md_victim->bk_nextsize->fd_nextsize = _md_victim;
                    }
                  else
                    {

                      while ((unsigned long) size < fwd->size)
                        {
                          fwd = fwd->fd_nextsize;
                        }

                      if ((unsigned long) size == (unsigned long) fwd->size)
                        /* Always insert in the second position.  */
                        fwd = fwd->fd;
                      else
                        {
                          _md_victim->fd_nextsize = fwd;
                          _md_victim->bk_nextsize = fwd->bk_nextsize;
                          fwd->bk_nextsize = _md_victim;
                          _md_victim->bk_nextsize->fd_nextsize = _md_victim;
                        }
                      bck = fwd->bk;
                    }
                }
              else
                _md_victim->fd_nextsize = _md_victim->bk_nextsize = _md_victim;
            }

          mark_bin (av, victim_index);
          _md_victim->bk = bck;
          _md_victim->fd = fwd;
          fwd->bk = _md_victim;
          bck->fd = _md_victim;

#define MAX_ITERS       10000
          if (++iters >= MAX_ITERS)
            break;
        }

      /*
        If a large request, scan through the chunks of current bin in
        sorted order to find smallest that fits.  Use the skip list for this.
      */

      if (!in_smallbin_range (nb))
        {
          bin = bin_at (av, idx);

          /* skip scan if empty or largest chunk is too small */
          if ((_md_victim = first (bin)) != bin &&
              (unsigned long) (_md_victim->size) >= (unsigned long) (nb))
            {
              _md_victim = _md_victim->bk_nextsize;
              while (((unsigned long) (size = chunksize (_md_victim)) <
                      (unsigned long) (nb)))
                _md_victim = _md_victim->bk_nextsize;

              /* Avoid removing the first entry for a size so that the skip
                 list does not have to be rerouted.  */
              if (_md_victim != last (bin) && _md_victim->size == _md_victim->fd->size)
                _md_victim = _md_victim->fd;

              remainder_size = size - nb;
              bin_unlink (av, _md_victim, &bck, &fwd);

              victim = chunkinfo2chunk(_md_victim);

              /* Exhaust */
              if (remainder_size < MINSIZE)
                {
                  set_inuse_bit_at_offset (av, _md_victim, victim, size);
                }
              /* Split */
              else
                {
                  remainder = chunk_at_offset (victim, nb);
                  /* We cannot assume the unsorted list is empty and therefore
                     have to perform a complete insert here.  */
                  bck = unsorted_chunks (av);
                  fwd = bck->fd;
                  if (__glibc_unlikely (fwd->bk != bck))
                    {
                      errstr = "malloc(): corrupted unsorted chunks";
                      goto errout;
                    }
                  set_head (_md_victim, nb | PREV_INUSE);
		  
                  _md_remainder = register_chunk(av, remainder, false, 11);

		  _md_temp = _md_victim->md_next;

		  _md_remainder->md_next = _md_temp;
		  _md_remainder->md_prev = _md_victim;
		  _md_victim->md_next = _md_remainder;

		  if(_md_temp != NULL){
		    _md_temp->md_prev = _md_remainder;
		    check_metadata(av, _md_temp);
		  }

                  set_head (_md_remainder, remainder_size | PREV_INUSE);
                  set_foot (av, _md_remainder, remainder, remainder_size);
		  check_metadata_chunk(av, remainder, _md_remainder);
		  check_metadata_chunk(av, victim, _md_victim);

                  _md_remainder->bk = bck;
                  _md_remainder->fd = fwd;
                  bck->fd = _md_remainder;
                  fwd->bk = _md_remainder;
                  if (!in_smallbin_range (remainder_size))
                    {
                      _md_remainder->fd_nextsize = NULL;
                      _md_remainder->bk_nextsize = NULL;
                    }

                }
              
              check_malloced_chunk (av, victim, _md_victim, nb);
              void *p = chunk2mem (victim);
              alloc_perturb (p, bytes);
              return _md_victim;
            }
        }

      /*
        Search for a chunk by scanning bins, starting with next largest
        bin. This search is strictly by best-fit; i.e., the smallest
        (with ties going to approximately the least recently used) chunk
        that fits is selected.

        The bitmap avoids needing to check that most blocks are nonempty.
        The particular case of skipping all bins during warm-up phases
        when no chunks have been returned yet is faster than it might look.
      */

      ++idx;
      bin = bin_at (av, idx);
      block = idx2block (idx);
      map = av->binmap[block];
      bit = idx2bit (idx);

      for (;; )
        {
          /* Skip rest of block if there are no more set bits in this block.  */
          if (bit > map || bit == 0)
            {
              do
                {
                  if (++block >= BINMAPSIZE) /* out of bins */
                    goto use_top;
                }
              while ((map = av->binmap[block]) == 0);

              bin = bin_at (av, (block << BINMAPSHIFT));
              bit = 1;
            }

          /* Advance to bin with set bit. There must be one. */
          while ((bit & map) == 0)
            {
              bin = next_bin (bin);
              bit <<= 1;
              assert (bit != 0);
            }

          /* Inspect the bin. It is likely to be non-empty */
          _md_victim = last (bin);

          /*  If a false alarm (empty bin), clear the bit. */
          if (_md_victim == bin)
            {
              av->binmap[block] = map &= ~bit; /* Write through */
              bin = next_bin (bin);
              bit <<= 1;
            }

          else
            {
              size = chunksize (_md_victim);
              
	      victim = chunkinfo2chunk(_md_victim);

              /*  We know the first chunk in this bin is big enough to use. */
              assert ((unsigned long) (size) >= (unsigned long) (nb));

              remainder_size = size - nb;

              /* unlink */
              bin_unlink (av, _md_victim, &bck, &fwd);

              /* Exhaust */
              if (remainder_size < MINSIZE)
                {
                  set_inuse_bit_at_offset (av, _md_victim, victim, size);
                }
              
              /* Split */
              else
                {
                  remainder = chunk_at_offset (victim, nb);
                  
                  /* We cannot assume the unsorted list is empty and therefore
                     have to perform a complete insert here.  */
                  bck = unsorted_chunks (av);
                  fwd = bck->fd;
                  if (__glibc_unlikely (fwd->bk != bck))
                    {
                      errstr = "malloc(): corrupted unsorted chunks 2";
                      goto errout;
                    }

                  set_head (_md_victim, nb | PREV_INUSE);

                  _md_remainder = register_chunk(av, remainder, false, 12);

		  _md_temp = _md_victim->md_next;

		  _md_remainder->md_next = _md_temp;
		  _md_remainder->md_prev = _md_victim;
		  _md_victim->md_next = _md_remainder;

		  if(_md_temp != NULL){
		    _md_temp->md_prev = _md_remainder;
		    check_metadata(av, _md_temp);
		  }
		  
                  set_head (_md_remainder, remainder_size | PREV_INUSE);
                  set_foot (av, _md_remainder, remainder, remainder_size);
		  check_metadata_chunk(av, remainder, _md_remainder);
		  check_metadata_chunk(av, victim, _md_victim);

                  _md_remainder->bk = bck;
                  _md_remainder->fd = fwd;
                  bck->fd = _md_remainder;
                  fwd->bk = _md_remainder;
                  
                  /* advertise as last remainder */
                  if (in_smallbin_range (nb)){
                    av->last_remainder = _md_remainder;
		  }
                  if (!in_smallbin_range (remainder_size))
                    {
                      _md_remainder->fd_nextsize = NULL;
                      _md_remainder->bk_nextsize = NULL;
                    }


                }
              
              check_malloced_chunk (av, victim, _md_victim, nb);
              void *p = chunk2mem (victim);
              alloc_perturb (p, bytes);
              return _md_victim;
            }
        }

    use_top:
      /*
        If large enough, split off the chunk bordering the end of memory
        (held in av->_md_top). Note that this is in accord with the best-fit
        search rule.  In effect, av->_md_top is treated as larger (and thus
        less well fitting) than any other available chunk since it can
        be extended to be as large as necessary (up to system
        limitations).

        We require that av->_md_top always exists (i.e., has size >=
        MINSIZE) after initialization, so if it would otherwise be
        exhausted by current request, it is replenished. (The main
        reason for ensuring it exists is that we may need MINSIZE space
        to put in fenceposts in sysmalloc.)
      */

      _md_victim = av->_md_top;
      victim = chunkinfo2chunk(_md_victim);
      size = chunksize (_md_victim);

      if ((unsigned long) (size) >= (unsigned long) (nb + MINSIZE))
        {


          av->_md_top = split_chunk(av, _md_victim, victim, size, nb);
          check_malloced_chunk (av, victim, _md_victim, nb);
          void *p = chunk2mem (victim);
          alloc_perturb (p, bytes);
          return _md_victim;
        }

      /* When we are using atomic ops to free fast chunks we can get
         here for all block sizes.  */
      else if (have_fastchunks (av))
        {
          malloc_consolidate (av);
          /* restore original bin index */
          if (in_smallbin_range (nb))
            idx = smallbin_index (nb);
          else
            idx = largebin_index (nb);
        }

      /*
        Otherwise, relay to handle system-dependent cases
      */
      else
        {
          _md_p  = sysmalloc (nb, av);
          mem = chunkinfo2mem(_md_p);
          if (mem != NULL)
            alloc_perturb (mem, bytes);
          return _md_p;
        }
    }
}

/*
  ------------------------------ free ------------------------------
*/

static void
_int_free (mstate av, chunkinfoptr _md_p, mchunkptr p, bool have_lock)
{
  INTERNAL_SIZE_T size;        /* its size */
  mfastbinptr *fb;             /* associated fastbin */
  mchunkptr temp;              /* temporary handle of chunk destined to be coalesced */
  chunkinfoptr _md_temp;       /* metadata of chunk destined to be coalesced */
  mchunkptr nextchunk;         /* next contiguous chunk */
  chunkinfoptr _md_nextchunk;  /* metadata of next contiguous chunk */
  INTERNAL_SIZE_T nextsize;    /* its size */
  int nextinuse;               /* true if nextchunk is used */
  INTERNAL_SIZE_T prevsize;    /* size of previous contiguous chunk */
  chunkinfoptr bck;            /* misc temp for linking */
  chunkinfoptr fwd;            /* misc temp for linking */

  chunkinfoptr _md_top;        /* metadata of the top chunk */
  mchunkptr topchunk;          /* the top chunk */

  
  const char *errstr = NULL;

  if(av == NULL){
    return;
  }

  _md_top = av->_md_top;
  topchunk = chunkinfo2chunk(_md_top);

  /*
    SRI:  We had to simplify the locking optimizations in this routine.

    Most calls we have:

    have_lock  && chunkinfo2chunk(_md_p) == p

    Sometimes we may have the lock but not _md_p (free_at_fork)
    
    Sometimes we have neither the lock nor _md_p (__libc_free)

  */

  assert( (av == NULL) || arena_from_chunk(p) );

  assert( (_md_p == NULL) || chunkinfo2chunk(_md_p) == p );

  /* SRI: we are forced to do this upfront because of the need to examine 'size' */

  if (!have_lock) {
    LOCK_ARENA(av, FREE_SITE);
  }

  if (_md_p == NULL) {
    _md_p = lookup_chunk(av, p);
    if (_md_p == NULL) { missing_metadata(av, p);  }
  } 
  
  check_top(av);

  size = chunksize (_md_p);  //SRI: need the metadata at this point.


  /* Little security check which won't hurt performance: the
     allocator never wraps around at the end of the address space.
     Therefore we can exclude some size values which might appear
     here by accident or by "design" from some intruder.  */
  if (__builtin_expect ((uintptr_t) p > (uintptr_t) -size, 0)
      || __builtin_expect (misaligned_chunk (p), 0))
    {
      errstr = "free(): invalid pointer";
    errout:
      if(!have_lock){
	UNLOCK_ARENA(av, FREE_SITE);
      }
      malloc_printerr (check_action, errstr, chunk2mem (p), av);
      return;
    }

  /* We know that each chunk is at least MINSIZE bytes in size or a
     multiple of MALLOC_ALIGNMENT.  */
  if (__glibc_unlikely (size < MINSIZE || !aligned_OK (size)))
    {
      errstr = "free(): invalid size";
      goto errout;
    }
  
  check_inuse_chunk(av, p, _md_p);
  
  nextchunk = chunk_at_offset(p, size);  
  //FIXME: Once twinned this lookup can be tossed.
  _md_nextchunk = lookup_chunk(av, nextchunk);
  if(_md_nextchunk == NULL){ missing_metadata(av, nextchunk); }
  nextsize = chunksize (_md_nextchunk);

  /*
    If eligible, place chunk on a fastbin so it can be found
    and used quickly in malloc.
  */

  if ((unsigned long)(size) <= (unsigned long)(get_max_fast ())
#if TRIM_FASTBINS
      /*
        If TRIM_FASTBINS set, don't place chunks
        bordering top into fastbins
      */
      && (nextchunk != topchunk)
#endif
      ) {
    
    if (__builtin_expect (_md_nextchunk->size <= 2 * SIZE_SZ, 0)
        || __builtin_expect (nextsize >= av->system_mem, 0))
      {
	errstr = "free(): invalid next size (fast)";
	goto errout;
      }
    
    
    free_perturb (chunk2mem(p), size - 2 * SIZE_SZ);
    
    set_fastchunks(av);
    unsigned int idx = fastbin_index(size);
    fb = &fastbin (av, idx);
    
    /* Atomically link P to its fastbin: P->FD = *FB; *FB = P;  */
    chunkinfoptr _md_old = *fb, _md_old2;
    unsigned int old_idx = ~0u;
    do
      {
        /* Check that the top of the bin is not the record we are going to add
           (i.e., double free).  */
        if (__builtin_expect (_md_old == _md_p, 0))
          {
            errstr = "double free or corruption (fasttop)";
            goto errout;
          }
        /* Check that size of fastbin chunk at the top is the same as
           size of the chunk that we are adding.  We can dereference OLD
           only if we have the lock, otherwise it might have already been
           deallocated.  See use of OLD_IDX below for the actual check.  */
        if (_md_old != NULL)
          old_idx = fastbin_index(chunksize(_md_old));
        _md_p->fd = _md_old2 = _md_old;
      }
    while ((_md_old = catomic_compare_and_exchange_val_rel (fb, _md_p, _md_old2)) != _md_old2);

    if (_md_old != NULL && __builtin_expect (old_idx != idx, 0))
      {
        errstr = "invalid fastbin entry (free)";
        goto errout;
      }

    if(!have_lock){
      UNLOCK_ARENA(av, FREE_SITE);
    }
  }

  /*
    Consolidate other non-mmapped chunks as they arrive.
  */

  else if (!chunk_is_mmapped(p)) {

    /* Lightweight tests: check whether the block is already the
       top block.  */
    if (__glibc_unlikely (p == topchunk))
      {
        errstr = "double free or corruption (top)";
        goto errout;
      }
    /* Or whether the next chunk is beyond the boundaries of the arena.  */
    if (__builtin_expect (contiguous (av)
                          && (char *) nextchunk
                          >= ((char *) topchunk + chunksize(av->_md_top)), 0))
      {
        errstr = "double free or corruption (out)";
        goto errout;
      }
    /* Or whether the block is actually not marked used.  */
    if (__glibc_unlikely (!prev_inuse(_md_nextchunk, nextchunk)))
      {
        errstr = "double free or corruption (!prev)";
        goto errout;
      }

    if (__builtin_expect (_md_nextchunk->size <= 2 * SIZE_SZ, 0)
        || __builtin_expect (nextsize >= av->system_mem, 0))
      {
        errstr = "free(): invalid next size (normal)";
        goto errout;
      }

    free_perturb (chunk2mem(p), size - 2 * SIZE_SZ);

    /* consolidate backward */
    if (!prev_inuse(_md_p, p)) {
      prevsize = _md_p->prev_size;
      size += prevsize;
      temp = p;
      _md_temp = _md_p;
      p = chunk_at_offset(p, -((long) prevsize));
      //FIXME: Once twinned this lookup can be tossed.
      _md_p = lookup_chunk(av, p);             
      if (_md_p == NULL) { missing_metadata(av, p); }           
      bin_unlink(av, _md_p, &bck, &fwd);
      /* correct the md_next pointer */
      _md_p->md_next = _md_temp->md_next;
      check_metadata_chunk(av, p, _md_p);

      /* do not leak the coalesced chunk's metadata */
      unregister_chunk(av, temp, 10); 
    }

    if (nextchunk != topchunk) {
      /* get and clear inuse bit */
      nextinuse = inuse_bit_at_offset(av, _md_nextchunk, nextchunk, nextsize);

      /* consolidate forward */
      if (!nextinuse) {
	size += nextsize;
        bin_unlink(av, _md_nextchunk, &bck, &fwd);

	/* correct the md_next & md_prev pointers */
	_md_temp = _md_nextchunk->md_next;
	_md_p->md_next = _md_temp;
	_md_temp->md_prev = _md_p;

	check_metadata_chunk(av, p, _md_p);

	/* do not leak the coalesced chunk's metadata */
	unregister_chunk(av, nextchunk, 11); 
      } else
        clear_inuse_bit_at_offset(av, _md_nextchunk, nextchunk, 0);

      /*
        Place the chunk in unsorted chunk list. Chunks are
        not placed into regular bins until after they have
        been given one chance to be used in malloc.
      */

      bck = unsorted_chunks(av);
      fwd = bck->fd;
      if (__glibc_unlikely (fwd->bk != bck))
        {
          errstr = "free(): corrupted unsorted chunks";
          goto errout;
        }
      _md_p->fd = fwd;
      _md_p->bk = bck;
      if (!in_smallbin_range(size))
        {
          _md_p->fd_nextsize = NULL;
          _md_p->bk_nextsize = NULL;
        }
      bck->fd = _md_p;
      fwd->bk = _md_p;


      set_head(_md_p, size | PREV_INUSE);
      set_foot(av, _md_p, p, size);

      check_top(av);
      
      check_free_chunk(av, p, _md_p);

    } 

    /*
      If the chunk borders the current high end of memory,
      consolidate into top
    */

    else {  /* nextchunk == topchunk */

      size += nextsize;
      set_head(_md_p, size | PREV_INUSE);
      av->_md_top = _md_p;
      /* fix the md_next pointer */
      _md_p->md_next = NULL;

      /* do not leak the coalesced top chunk's metadata */
      unregister_chunk(av, topchunk, 12); 
      check_chunk(av, p, _md_p);
    }

    /*
      If freeing a large space, consolidate possibly-surrounding
      chunks. Then, if the total unused topmost memory exceeds trim
      threshold, ask malloc_trim to reduce top.

      Unless max_fast is 0, we don't know if there are fastbins
      bordering top, so we cannot tell for sure whether threshold
      has been reached unless fastbins are consolidated.  But we
      don't want to consolidate on each free.  As a compromise,
      consolidation is performed if FASTBIN_CONSOLIDATION_THRESHOLD
      is reached.
    */

    if ((unsigned long)(size) >= FASTBIN_CONSOLIDATION_THRESHOLD) {
      if (have_fastchunks(av))
        malloc_consolidate(av);

      if (av == &main_arena) {
#ifndef MORECORE_CANNOT_TRIM
        if ((unsigned long)(chunksize(av->_md_top)) >=
            (unsigned long)(mp_.trim_threshold))
          systrim(mp_.top_pad, av);
#endif
      } else {
        /* Always try heap_trim(), even if the top chunk is not
           large, because the corresponding heap might go away.  */
        heap_info *heap = heap_for_ptr(topchunk);

        assert(heap->ar_ptr == av);
	check_top(av);
        heap_trim(heap, mp_.top_pad);
	check_top(av);
      }
    }
    
    if(!have_lock){
      UNLOCK_ARENA(av, FREE_SITE);
    }

  }
  /*
    If the chunk was allocated via mmap, release via munmap().
  */

  else {

    if(av != &main_arena){
      if(!have_lock){
	UNLOCK_ARENA(av, FREE_SITE);
      }
      LOCK_ARENA(&main_arena, FREE_SITE);
    
      munmap_chunk (_md_p);
      unregister_chunk(&main_arena, p, false);
      //Done:  md_next = md_prev = NULL 
      UNLOCK_ARENA(&main_arena, FREE_SITE);
    } else {

      munmap_chunk (_md_p);
      unregister_chunk(&main_arena, p, false);
      //Done: md_next = md_prev = NULL 
      if(!have_lock){
	UNLOCK_ARENA(&main_arena, FREE_SITE);
      }

    }
  }

}

/*
  ------------------------- malloc_consolidate -------------------------

  malloc_consolidate is a specialized version of free() that tears
  down chunks held in fastbins.  Free itself cannot be used for this
  purpose since, among other things, it might place chunks back onto
  fastbins.  So, instead, we need to use a minor variant of the same
  code.

  Also, because this routine needs to be called the first time through
  malloc anyway, it turns out to be the perfect place to trigger
  initialization code.
*/

static void malloc_consolidate(mstate av)
{
  mfastbinptr*    fb;                 /* current fastbin being consolidated */
  mfastbinptr*    maxfb;              /* last fastbin (for loop control) */
  mchunkptr       p;                  /* current chunk being consolidated */
  mchunkptr       temp;               /* temporary handle on current chunk being consolidated */
  chunkinfoptr    _md_temp;           /* temporary handle on the metatdata of the chunk being coalesced */
  chunkinfoptr    _md_p;              /* metatdata of current chunk being consolidated */
  chunkinfoptr    _md_nextp;          /* metadata of next chunk to consolidate */
  
  chunkinfoptr    unsorted_bin;       /* bin header */
  chunkinfoptr    first_unsorted;     /* chunk to link to */

  /* These have same use as in free() */
  mchunkptr       nextchunk;
  chunkinfoptr    _md_nextchunk;
  INTERNAL_SIZE_T size;
  INTERNAL_SIZE_T nextsize;
  INTERNAL_SIZE_T prevsize;
  int             nextinuse;
  chunkinfoptr    bck;
  chunkinfoptr    fwd;

  mchunkptr topchunk;


  /*
    If max_fast is 0, we know that av hasn't
    yet been initialized, in which case do so below
  */

  if (get_max_fast () != 0) {

    clear_fastchunks(av);

    unsorted_bin = unsorted_chunks(av);

    /*
      Remove each chunk from fast bin and consolidate it, placing it
      then in unsorted bin. Among other reasons for doing this,
      placing in unsorted bin avoids needing to calculate actual bins
      until malloc is sure that chunks aren't immediately going to be
      reused anyway.
    */

    maxfb = &fastbin (av, NFASTBINS - 1);
    fb = &fastbin (av, 0);
    do {
      _md_p = atomic_exchange_acq (fb, 0);
      if (_md_p != 0) {
        do {
	  p = chunkinfo2chunk(_md_p);
          check_inuse_chunk(av, p, _md_p);
          _md_nextp = _md_p->fd;

          /* Slightly streamlined version of consolidation code in free() */
          size = _md_p->size & ~PREV_INUSE;
          nextchunk = chunk_at_offset(p, size);
	  //FIXME: Once twinned this lookup can be tossed.
	  _md_nextchunk = lookup_chunk(av, nextchunk);
	  if (_md_nextchunk == NULL) { missing_metadata(av, nextchunk); }
          nextsize = chunksize(_md_nextchunk);

          if (!prev_inuse(_md_p, p)) {
            prevsize = _md_p->prev_size;
            size += prevsize;
	    temp = p;
	    _md_temp = _md_p;
            p = chunk_at_offset(p, -((long) prevsize));
	    //FIXME: Once twinned this lookup can be tossed.
            _md_p = lookup_chunk (av, p);
	    if (_md_p == NULL) { missing_metadata(av, p); }
            bin_unlink(av, _md_p, &bck, &fwd);
	    /* correct the md_next pointer */
	    _md_p->md_next = _md_temp->md_next;
	    
	    check_metadata_chunk(av, p, _md_p);

	    /* do not leak the coalesced chunk's metadata */
	    unregister_chunk(av, temp, 7); 
          }

	  topchunk = chunkinfo2chunk(av->_md_top);


          if (nextchunk != topchunk) {
            nextinuse = inuse_bit_at_offset(av, _md_nextchunk, nextchunk, nextsize);

            if (!nextinuse) {
              size += nextsize;
              bin_unlink(av, _md_nextchunk, &bck, &fwd);

	      /* correct the md_next & md_prev pointers */
	      _md_temp = _md_nextchunk->md_next;
	      _md_p->md_next = _md_temp;
		_md_temp->md_prev = _md_p;
   
	      check_metadata_chunk(av, p, _md_p);

	      /* do not leak the coalesced chunk's metadata */
	      unregister_chunk(av, nextchunk, 8); 
            } else
              clear_inuse_bit_at_offset(av, _md_nextchunk, nextchunk, 0);

            first_unsorted = unsorted_bin->fd;
            unsorted_bin->fd = _md_p;
            first_unsorted->bk = _md_p;

            if (!in_smallbin_range (size)) {
              _md_p->fd_nextsize = NULL;
              _md_p->bk_nextsize = NULL;
            }

            set_head(_md_p, size | PREV_INUSE);
            _md_p->bk = unsorted_bin;
            _md_p->fd = first_unsorted;
            set_foot(av, _md_p, p, size);
          
	  }

          else { /* nextchunk == topchunk */
	    
            size += nextsize;
            set_head(_md_p, size | PREV_INUSE);
            av->_md_top = _md_p;
	    /* fix the md_next pointer */
	    _md_p->md_next = NULL;

	    /* do not leak the old top's chunk's metadata */
	    unregister_chunk(av, topchunk, 9); 
            check_top(av);
          }

        } while ( (_md_p = _md_nextp) != 0);
	
      }
    } while (fb++ != maxfb);
  }
  else {
    malloc_init_state(av, true);
    check_malloc_state(av);
  }
}

/*
  ------------------------------ realloc ------------------------------
*/

chunkinfoptr
_int_realloc(mstate av, chunkinfoptr _md_oldp, INTERNAL_SIZE_T oldsize,
             INTERNAL_SIZE_T nb)
{
  mchunkptr        newp;            /* chunk to return */
  chunkinfoptr     _md_newp;        /* metadata of chunk to return */
  mchunkptr        oldp;            /* old chunk */
  INTERNAL_SIZE_T  newsize;         /* its size */
  void*            newmem;          /* corresponding user mem */

  mchunkptr        malloced_chunk;  /* keep track of the malloced chunk */

  mchunkptr        next;            /* next contiguous chunk after oldp */
  chunkinfoptr     _md_next;        /* metadata of next contiguous chunk after oldp */

  chunkinfoptr     _md_temp;        /* temporary handle to metadata  */

  mchunkptr        remainder;       /* extra space at end of newp */
  chunkinfoptr     _md_remainder;   /* metadata for the extra space at end of newp */
  unsigned long    remainder_size;  /* its size */

  chunkinfoptr     bck;             /* misc temp for linking */
  chunkinfoptr     fwd;             /* misc temp for linking */

  unsigned long    copysize;        /* bytes to copy */
  unsigned int     ncopies;         /* INTERNAL_SIZE_T words to copy */
  INTERNAL_SIZE_T* s;               /* copy source */
  INTERNAL_SIZE_T* d;               /* copy destination */

  mchunkptr    topchunk;            /* chunk of av->_md_top  */


  const char *errstr = NULL;

  oldp = chunkinfo2chunk(_md_oldp);

  /* oldmem size */
  if (__builtin_expect (_md_oldp->size <= 2 * SIZE_SZ, 0)
      || __builtin_expect (oldsize >= av->system_mem, 0))
    {
      errstr = "realloc(): invalid old size";
    errout:
      malloc_printerr (check_action, errstr, chunk2mem (oldp), av);
      return 0;
    }


  check_inuse_chunk (av, oldp, _md_oldp);

  /* All callers already filter out mmap'ed chunks.  */
  assert (!chunk_is_mmapped (oldp));

  next = chunk_at_offset (oldp, oldsize);
  _md_next = lookup_chunk(av, next);
  if (_md_next == NULL) { missing_metadata(av, next); }
  
  topchunk = chunkinfo2chunk(av->_md_top);                

  INTERNAL_SIZE_T nextsize = chunksize (_md_next);
  if (__builtin_expect (_md_next->size <= 2 * SIZE_SZ, 0)
      || __builtin_expect (nextsize >= av->system_mem, 0))
    {
      errstr = "realloc(): invalid next size";
      goto errout;
    }

  if ((unsigned long) (oldsize) >= (unsigned long) (nb))
    {
      /* already big enough; split below */
      newp = oldp;
      _md_newp = _md_oldp;
      newsize = oldsize;
    }

  else
    {
      /* Try to expand forward into topchunk */
      if (next == topchunk &&
          (unsigned long) (newsize = oldsize + nextsize) >=
          (unsigned long) (nb + MINSIZE))
        {
          /* SRI: we are going to move top nb bytes along; so we'll need to provide new metadata */ 
          unregister_chunk(av, topchunk, 1);

          /* update oldp's metadata */
          set_head_size (_md_oldp, nb);

          /* move top along nb bytes */
          topchunk = chunk_at_offset (oldp, nb);
          /* removing invalidates; need to get a fresh one */
          av->_md_top = register_chunk(av, topchunk, false, 13);

	  _md_oldp->md_next = av->_md_top;
	  av->_md_top->md_prev = _md_oldp;
	  av->_md_top->md_next = NULL;

          set_head (av->_md_top, (newsize - nb) | PREV_INUSE);

          check_inuse_chunk (av, oldp, _md_oldp);
	  check_top(av);

          return _md_oldp;
        }

      /* Try to expand forward into next chunk;  split off remainder below */
      else if (next != topchunk &&
               !inuse(av, _md_next, next) &&
               (unsigned long) (newsize = oldsize + nextsize) >=
               (unsigned long) (nb))
        {

          newp = oldp;
	  
	  /* fix the md_next and md_prev pointers */
	  _md_temp = _md_next->md_next;
	  if(_md_temp != NULL)
	    _md_temp->md_prev = _md_oldp;
	  _md_oldp->md_next = _md_temp;

	  check_metadata_chunk(av, oldp, _md_oldp);

          bin_unlink (av, _md_next, &bck, &fwd);
          /* don't leak next's metadata */
          unregister_chunk(av, next, 2); 

        }

      /* allocate, copy, free */
      else
        {
          _md_newp = _int_malloc (av, nb - MALLOC_ALIGN_MASK);

          if (_md_newp == 0)
            return 0; /* propagate failure */
          
          newp = chunkinfo2chunk(_md_newp);
          newmem = chunkinfo2mem(_md_newp);
          malloced_chunk = newp;

          newsize = chunksize (_md_newp);

          /*
            Avoid copy if newp is next chunk after oldp.
          */
          if (newp == next)
            {
              newsize += oldsize;
              newp = oldp;  /* now we have newp != malloced_chunk */

	      /* fix the md_next and md_prev pointers */
	      _md_temp = _md_newp->md_next;
	      if(_md_temp != NULL)
		_md_temp->md_prev = _md_oldp;
	      _md_oldp->md_next = _md_temp;

	      check_metadata_chunk(av, oldp, _md_oldp);

              unregister_chunk(av, malloced_chunk, 3); 

            }
          else
            {
              /*
                Unroll copy of <= 36 bytes (72 if 8byte sizes)
                We know that contents have an odd number of
                INTERNAL_SIZE_T-sized words; minimally 3.
              */

              copysize = oldsize - SIZE_SZ;
              s = (INTERNAL_SIZE_T *) (chunk2mem (oldp));
              d = (INTERNAL_SIZE_T *) (newmem);
              ncopies = copysize / sizeof (INTERNAL_SIZE_T);
              assert (ncopies >= 3);

              if (ncopies > 9)
                memcpy (d, s, copysize);

              else
                {
                  *(d + 0) = *(s + 0);
                  *(d + 1) = *(s + 1);
                  *(d + 2) = *(s + 2);
                  if (ncopies > 4)
                    {
                      *(d + 3) = *(s + 3);
                      *(d + 4) = *(s + 4);
                      if (ncopies > 6)
                        {
                          *(d + 5) = *(s + 5);
                          *(d + 6) = *(s + 6);
                          if (ncopies > 8)
                            {
                              *(d + 7) = *(s + 7);
                              *(d + 8) = *(s + 8);
                            }
                        }
                    }
                }

              _int_free (av, _md_oldp, oldp, 1);
              check_inuse_chunk (av, newp, _md_newp);
              return _md_newp;
            }
          
          /* SRI: need to be careful here. newp == oldp */

        }
    } /* SRI: hunk is already big enough */

  assert(newp == oldp);

  if (newp != oldp) {
    return 0;
  }

  _md_newp = _md_oldp;

  /* If possible, free extra space in old or extended chunk */

  assert ((unsigned long) (newsize) >= (unsigned long) (nb));

  remainder_size = newsize - nb;

  if (remainder_size < MINSIZE)   /* not enough extra to split off */
    {
      set_head_size (_md_newp, newsize);
      set_inuse_bit_at_offset (av, _md_newp, newp, newsize);
    }
  else   /* split remainder */
    {

      set_head_size (_md_newp, nb);
      remainder = chunk_at_offset (newp, nb);
      _md_remainder = register_chunk(av, remainder, false, 14);

      
      _md_temp = _md_newp->md_next;

      _md_remainder->md_next = _md_temp;
      _md_remainder->md_prev = _md_newp;
      _md_newp->md_next =  _md_remainder;

      if(_md_temp != NULL){
	_md_temp->md_prev = _md_remainder;
	check_metadata(av,_md_newp);
      }
      

      check_metadata_chunk(av,remainder,_md_remainder);
      check_metadata_chunk(av,newp,_md_newp);

      set_head (_md_remainder, remainder_size | PREV_INUSE);
      /* Mark remainder as inuse so free() won't complain */
      set_inuse_bit_at_offset (av, _md_remainder, remainder, remainder_size);

      
      _int_free (av, _md_remainder, remainder, 1);
    }

  check_inuse_chunk (av, newp, _md_newp);
    
  return _md_newp;
}

/*
  ------------------------------ memalign ------------------------------
*/

static chunkinfoptr
_int_memalign (mstate av, size_t alignment, size_t bytes)
{
  INTERNAL_SIZE_T nb;             /* padded  request size */
  char *m;                        /* memory returned by malloc call */
  chunkinfoptr _md_p;             /* metadata of memory returned by malloc call */
  mchunkptr p;                    /* corresponding chunk returned by malloc call */
  char *brk;                      /* alignment point within p */
  mchunkptr newp;                 /* chunk to return */
  chunkinfoptr _md_newp;          /* metadata of chunk to return */
  INTERNAL_SIZE_T newsize;        /* its size */
  INTERNAL_SIZE_T leadsize;       /* leading space before alignment point */
  mchunkptr remainder;            /* spare room at end to split off */
  chunkinfoptr _md_remainder;     /* metadata of spare room at end to split off */
  unsigned long remainder_size;   /* its size */
  INTERNAL_SIZE_T size;

  /*  
      This is an entry point so needs a replenish, but we rely on the call
      to _int_malloc to do that with enough left over (2) for this.
  */

  if ( !checked_request2size (bytes, &nb) ) {
    return 0;
  }

  /*
    Strategy: find a spot within that chunk that meets the alignment
    request, and then possibly free the leading and trailing space.
  */


  /* Call malloc with worst case padding to hit alignment. */

  _md_p = _int_malloc (av, nb + alignment + MINSIZE);
  m = (char *) (chunkinfo2mem(_md_p));

  if (m == 0)
    return 0;           /* propagate failure */

  p = mem2chunk (m);

  if ((((unsigned long) (m)) % alignment) != 0)   /* misaligned */

    { /*
        Find an aligned spot inside chunk.  Since we need to give back
        leading space in a chunk of at least MINSIZE, if the first
        calculation places us at a spot with less than MINSIZE leader,
        we can move to the next aligned spot -- we've allocated enough
        total room so that this is always possible.
      */
      brk = (char *) mem2chunk ((void *)(((unsigned long) (m + alignment - 1)) &
                                         - ((signed long) alignment)));
      if ((unsigned long) (brk - (char *) (p)) < MINSIZE)
        brk += alignment;

      newp = (mchunkptr) brk;
      leadsize = brk - (char *) (p);
      newsize = chunksize (_md_p) - leadsize;

      /* For mmapped chunks, just adjust offset */
      if (chunk_is_mmapped (p))
        {
	  /* make sure we the main_arena is good to handle this request */
	  if (av != &main_arena) {
	    /* if we are already in the main arena we should, by design, have enough metadata */
	    if(av != NULL){
	      UNLOCK_ARENA(av, MEMALIGN_SITE);
	    }
	    LOCK_ARENA(&main_arena, MEMALIGN_SITE);
	    if(main_arena.metadata_cache_count == 0){
	      UNLOCK_ARENA(&main_arena, MEMALIGN_SITE);
	      if(av != NULL){
		 LOCK_ARENA(av, MEMALIGN_SITE);
	      }
	      return NULL;
	    }
	  }

          _md_newp = register_chunk(&main_arena, newp, true, 15);
          _md_newp->prev_size = _md_p->prev_size + leadsize;
	  _md_newp->md_next = _md_newp->md_prev = NULL;

	  check_metadata_chunk(av, newp, _md_newp);

          set_head (_md_newp, newsize);

	  UNLOCK_ARENA(&main_arena, MEMALIGN_SITE);
	  if(av != NULL){
	    LOCK_ARENA(av, MEMALIGN_SITE);
	  }

          return _md_newp;
        }

      /* Otherwise, give back leader, use the rest */
      _md_newp = register_chunk(av, newp, false, 16);

      _md_newp->md_next = _md_p->md_next;
      _md_newp->md_prev = _md_p;
      _md_p->md_next = _md_newp;

      check_metadata_chunk(av, newp, _md_newp);

      set_head (_md_newp, newsize | PREV_INUSE);

      set_inuse_bit_at_offset (av, _md_newp, newp, newsize);
      set_head_size (_md_p, leadsize);
      _int_free (av, _md_p, p, 1);
      p = newp;
      _md_p = _md_newp;
      assert (newsize >= nb &&
              (((unsigned long) (chunk2mem (p))) % alignment) == 0);
    }

  /* Also give back spare room at the end */
  if (!chunk_is_mmapped (p))
    {
      size = chunksize (_md_p);
      if ((unsigned long) (size) > (unsigned long) (nb + MINSIZE))
        {
          remainder_size = size - nb;
          remainder = chunk_at_offset (p, nb);
          _md_remainder = register_chunk(av, remainder, false, 17);

	  _md_remainder->md_next = _md_p->md_next;
	  _md_remainder->md_prev = _md_p;
	  _md_p->md_next = _md_remainder;
	  check_metadata_chunk(av,remainder,_md_remainder);
	  check_metadata_chunk(av,p,_md_p);

          set_head (_md_remainder, remainder_size | PREV_INUSE);
          set_head_size (_md_p, nb);


          _int_free (av, _md_remainder, remainder, 1);
        }
    }

  check_inuse_chunk (av, p, _md_newp);
  return _md_p;
}


/*
  ------------------------------ malloc_trim ------------------------------
*/

static int
mtrim (mstate av, size_t pad)
{
  /* Don't touch corrupt arenas.  */
  if (arena_is_corrupt (av))
    return 0;

  /* Ensure initialization/consolidation */
  malloc_consolidate (av);

  const size_t ps = GLRO (dl_pagesize);
  int psindex = bin_index (ps);
  const size_t psm1 = ps - 1;

  int result = 0;
  for (int i = 1; i < NBINS; ++i)
    if (i == 1 || i >= psindex)
      {
        mbinptr bin = bin_at (av, i);

        for (chunkinfoptr _md_p = last (bin); _md_p != bin; _md_p = _md_p->bk)
          {
            INTERNAL_SIZE_T size = chunksize (_md_p);
	    mchunkptr p = chunkinfo2chunk(_md_p);

            if (size > psm1 + sizeof (struct malloc_chunk))
              {
                /* See whether the chunk contains at least one unused page.  */
                char *paligned_mem = (char *) (((uintptr_t) p
                                                + sizeof (struct malloc_chunk)
                                                + psm1) & ~psm1);

                assert ((char *) chunk2mem (p) + 4 * SIZE_SZ <= paligned_mem);
                assert ((char *) p + size > paligned_mem);

                /* This is the size we could potentially free.  */
                size -= paligned_mem - (char *) p;

                if (size > psm1)
                  {
#if MALLOC_DEBUG
                    /* When debugging we simulate destroying the memory
                       content.  */
                    memset (paligned_mem, 0x89, size & ~psm1);
#endif
                    __madvise (paligned_mem, size & ~psm1, MADV_DONTNEED);

                    result = 1;
                  }
              }
          }
      }

#ifndef MORECORE_CANNOT_TRIM
  return result | (av == &main_arena ? systrim (pad, av) : 0);

#else
  return result;
#endif
}


int
__malloc_trim (size_t s)
{
  int result = 0;

  if (__malloc_initialized < 0)
    ptmalloc_init ();

  mstate ar_ptr = &main_arena;
  do
    {
      LOCK_ARENA(ar_ptr, TRIM_SITE);
      result |= mtrim (ar_ptr, s);
      UNLOCK_ARENA(ar_ptr, TRIM_SITE);

      ar_ptr = ar_ptr->next;
    }
  while (ar_ptr != &main_arena);

  return result;
}


/*
  ------------------------- malloc_usable_size -------------------------
*/

static size_t
musable (void *mem)
{
  mstate ar_ptr;
  mchunkptr p;
  chunkinfoptr _md_p;
  size_t retval;

  retval = 0;

  if (mem != 0)
    {
      p = mem2chunk (mem);

      ar_ptr = arena_for_chunk(p);

      

      LOCK_ARENA(ar_ptr, MUSABLE_SITE);

      _md_p = lookup_chunk(ar_ptr, p);

      if (_md_p == NULL) {
	missing_metadata(ar_ptr, p);
      } 
      
      if (__builtin_expect (using_malloc_checking == 1, 0)){
        retval =  malloc_check_get_size (ar_ptr, _md_p, p);
      } 
      else if (chunk_is_mmapped(p)){
	retval = chunksize(_md_p) - 2*SIZE_SZ; 
      }
      else if (inuse(ar_ptr, _md_p, p)){
	retval = chunksize(_md_p) - SIZE_SZ; 
      }
      
      UNLOCK_ARENA(ar_ptr, MUSABLE_SITE);
      
    }
  return retval;
}

size_t
__malloc_usable_size (void *m)
{
  size_t result;

  result = musable (m);
  return result;
}

/*
  ------------------------------ mallinfo ------------------------------
  Accumulate malloc statistics for arena AV into M.
*/

static void
int_mallinfo (mstate av, struct mallinfo *m)
{
  size_t i;
  mbinptr b;
  chunkinfoptr _md_p;
  INTERNAL_SIZE_T avail;
  INTERNAL_SIZE_T fastavail;
  int nblocks;
  int nfastblocks;

  /* Ensure initialization */
  /* SRI: kind of inconsistent with the initialization checks */
  if (av->_md_top == 0)            
    malloc_consolidate (av);

  check_malloc_state (av);

  /* Account for top */
  avail = chunksize (av->_md_top);
  nblocks = 1;  /* top always exists */

  /* traverse fastbins */
  nfastblocks = 0;
  fastavail = 0;

  for (i = 0; i < NFASTBINS; ++i)
    {
      for (_md_p = fastbin (av, i); _md_p != 0; _md_p = _md_p->fd)
        {
          ++nfastblocks;
          fastavail += chunksize (_md_p);
        }
    }

  avail += fastavail;

  /* traverse regular bins */
  for (i = 1; i < NBINS; ++i)
    {
      b = bin_at (av, i);
      for (_md_p = last (b); _md_p != b; _md_p = _md_p->bk)
        {
          ++nblocks;
          avail += chunksize (_md_p);
        }
    }

  m->smblks += nfastblocks;
  m->ordblks += nblocks;
  m->fordblks += avail;
  m->uordblks += av->system_mem - avail;
  m->arena += av->system_mem;
  m->fsmblks += fastavail;
  if (av == &main_arena)
    {
      m->hblks = mp_.n_mmaps;
      m->hblkhd = mp_.mmapped_mem;
      m->usmblks = mp_.max_total_mem;
      m->keepcost = chunksize (av->_md_top);
    }
}


struct mallinfo
__libc_mallinfo (void)
{
  struct mallinfo m;
  mstate ar_ptr;

  if (__malloc_initialized < 0)
    ptmalloc_init ();

  memset (&m, 0, sizeof (m));
  ar_ptr = &main_arena;
  do
    {
      LOCK_ARENA(ar_ptr, MALLINFO_SITE);
      int_mallinfo (ar_ptr, &m);
      UNLOCK_ARENA(ar_ptr, MALLINFO_SITE);

      ar_ptr = ar_ptr->next;
    }
  while (ar_ptr != &main_arena);

  return m;
}

/*
  ------------------------------ malloc_stats ------------------------------
*/

void
__malloc_stats (void)
{
  int i;
  mstate ar_ptr;
  unsigned int in_use_b = mp_.mmapped_mem, system_b = in_use_b;

  if (__malloc_initialized < 0)
    ptmalloc_init ();
  _IO_flockfile (stderr);
  int old_flags2 = ((_IO_FILE *) stderr)->_flags2;
  ((_IO_FILE *) stderr)->_flags2 |= _IO_FLAGS2_NOTCANCEL;
  for (i = 0, ar_ptr = &main_arena;; i++)
    {
      struct mallinfo mi;

      memset (&mi, 0, sizeof (mi));
      LOCK_ARENA(ar_ptr, MALLOC_STATS_SITE);
      int_mallinfo (ar_ptr, &mi);
      fprintf (stderr, "Arena %zu:\n", ar_ptr->arena_index);
      fprintf (stderr, "system bytes     = %10u\n", (unsigned int) mi.arena);
      fprintf (stderr, "in use bytes     = %10u\n", (unsigned int) mi.uordblks);
      dump_metadata(stderr, &(ar_ptr->htbl), false);
#if MALLOC_DEBUG > 1
      if (i > 0)
        dump_heap (heap_for_ptr (chunkinfo2chunk(ar_ptr->_md_top)));
#endif
      system_b += mi.arena;
      in_use_b += mi.uordblks;
      UNLOCK_ARENA(ar_ptr, MALLOC_STATS_SITE);
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
  fprintf (stderr, "Total (incl. mmap):\n");
  fprintf (stderr, "system bytes     = %10u\n", system_b);
  fprintf (stderr, "in use bytes     = %10u\n", in_use_b);
  fprintf (stderr, "max mmap regions = %10u\n", (unsigned int) mp_.max_n_mmaps);
  fprintf (stderr, "max mmap bytes   = %10lu\n",
           (unsigned long) mp_.max_mmapped_mem);
  lookup_dump(stderr);
  ((_IO_FILE *) stderr)->_flags2 |= old_flags2;
  _IO_funlockfile (stderr);
}


/*
  ------------------------------ mallopt ------------------------------
*/

int
__libc_mallopt (int param_number, int value)
{
  mstate av = &main_arena;
  int res = 1;

  if (__malloc_initialized < 0)
    ptmalloc_init ();
  LOCK_ARENA(av, MALLOPT_SITE);
  /* Ensure initialization/consolidation */
  malloc_consolidate (av);

  LIBC_PROBE (memory_mallopt, 2, param_number, value);

  switch (param_number)
    {
    case M_MXFAST:
      if (value >= 0 && value <= MAX_FAST_SIZE)
        {
          LIBC_PROBE (memory_mallopt_mxfast, 2, value, get_max_fast ());
          set_max_fast (value);
        }
      else
        res = 0;
      break;

    case M_TRIM_THRESHOLD:
      LIBC_PROBE (memory_mallopt_trim_threshold, 3, value,
                  mp_.trim_threshold, mp_.no_dyn_threshold);
      mp_.trim_threshold = value;
      mp_.no_dyn_threshold = 1;
      break;

    case M_TOP_PAD:
      LIBC_PROBE (memory_mallopt_top_pad, 3, value,
                  mp_.top_pad, mp_.no_dyn_threshold);
      mp_.top_pad = value;
      mp_.no_dyn_threshold = 1;
      break;

    case M_MMAP_THRESHOLD:
      /* Forbid setting the threshold too high. */
      if ((unsigned long) value > HEAP_MAX_SIZE / 2)
        res = 0;
      else
        {
          LIBC_PROBE (memory_mallopt_mmap_threshold, 3, value,
                      mp_.mmap_threshold, mp_.no_dyn_threshold);
          mp_.mmap_threshold = value;
          mp_.no_dyn_threshold = 1;
        }
      break;

    case M_MMAP_MAX:
      LIBC_PROBE (memory_mallopt_mmap_max, 3, value,
                  mp_.n_mmaps_max, mp_.no_dyn_threshold);
      mp_.n_mmaps_max = value;
      mp_.no_dyn_threshold = 1;
      break;

    case M_CHECK_ACTION:
      LIBC_PROBE (memory_mallopt_check_action, 2, value, check_action);
      check_action = value;
      break;

    case M_PERTURB:
      LIBC_PROBE (memory_mallopt_perturb, 2, value, perturb_byte);
      perturb_byte = value;
      break;

    case M_ARENA_TEST:
      if (value > 0)
        {
          LIBC_PROBE (memory_mallopt_arena_test, 2, value, mp_.arena_test);
          mp_.arena_test = value;
        }
      break;

    case M_ARENA_MAX:
      if (value > 0)
        {
          LIBC_PROBE (memory_mallopt_arena_max, 2, value, mp_.arena_max);
          mp_.arena_max = value;
        }
      break;
    }
  UNLOCK_ARENA(av, MALLOPT_SITE);
  return res;
}
libc_hidden_def (__libc_mallopt)


/*
  -------------------- Alternative MORECORE functions --------------------
*/


/*
  General Requirements for MORECORE.

  The MORECORE function must have the following properties:

  If MORECORE_CONTIGUOUS is false:

  * MORECORE must allocate in multiples of pagesize. It will
  only be called with arguments that are multiples of pagesize.

  * MORECORE(0) must return an address that is at least
  MALLOC_ALIGNMENT aligned. (Page-aligning always suffices.)

  else (i.e. If MORECORE_CONTIGUOUS is true):

  * Consecutive calls to MORECORE with positive arguments
  return increasing addresses, indicating that space has been
  contiguously extended.

  * MORECORE need not allocate in multiples of pagesize.
  Calls to MORECORE need not have args of multiples of pagesize.

  * MORECORE need not page-align.

  In either case:

  * MORECORE may allocate more memory than requested. (Or even less,
  but this will generally result in a malloc failure.)

  * MORECORE must not allocate memory when given argument zero, but
  instead return one past the end address of memory from previous
  nonzero call. This malloc does NOT call MORECORE(0)
  until at least one call with positive arguments is made, so
  the initial value returned is not important.

  * Even though consecutive calls to MORECORE need not return contiguous
  addresses, it must be OK for malloc'ed chunks to span multiple
  regions in those cases where they do happen to be contiguous.

  * MORECORE need not handle negative arguments -- it may instead
  just return MORECORE_FAILURE when given negative arguments.
  Negative arguments are always multiples of pagesize. MORECORE
  must not misinterpret negative args as large positive unsigned
  args. You can suppress all such calls from even occurring by defining
  MORECORE_CANNOT_TRIM,

  There is some variation across systems about the type of the
  argument to sbrk/MORECORE. If size_t is unsigned, then it cannot
  actually be size_t, because sbrk supports negative args, so it is
  normally the signed type of the same width as size_t (sometimes
  declared as "intptr_t", and sometimes "ptrdiff_t").  It doesn't much
  matter though. Internally, we use "long" as arguments, which should
  work across all reasonable possibilities.

  Additionally, if MORECORE ever returns failure for a positive
  request, then mmap is used as a noncontiguous system allocator. This
  is a useful backup strategy for systems with holes in address spaces
  -- in this case sbrk cannot contiguously expand the heap, but mmap
  may be able to map noncontiguous space.

  If you'd like mmap to ALWAYS be used, you can define MORECORE to be
  a function that always returns MORECORE_FAILURE.

  If you are using this malloc with something other than sbrk (or its
  emulation) to supply memory regions, you probably want to set
  MORECORE_CONTIGUOUS as false.  As an example, here is a custom
  allocator kindly contributed for pre-OSX macOS.  It uses virtually
  but not necessarily physically contiguous non-paged memory (locked
  in, present and won't get swapped out).  You can use it by
  uncommenting this section, adding some #includes, and setting up the
  appropriate defines above:

  *#define MORECORE osMoreCore
  *#define MORECORE_CONTIGUOUS 0

  There is also a shutdown routine that should somehow be called for
  cleanup upon program exit.

  *#define MAX_POOL_ENTRIES 100
  *#define MINIMUM_MORECORE_SIZE  (64 * 1024)
  static int next_os_pool;
  void *our_os_pools[MAX_POOL_ENTRIES];

  void *osMoreCore(int size)
  {
  void *ptr = 0;
  static void *sbrk_top = 0;

  if (size > 0)
  {
  if (size < MINIMUM_MORECORE_SIZE)
  size = MINIMUM_MORECORE_SIZE;
  if (CurrentExecutionLevel() == kTaskLevel)
  ptr = PoolAllocateResident(size + RM_PAGE_SIZE, 0);
  if (ptr == 0)
  {
  return (void *) MORECORE_FAILURE;
  }
  // save ptrs so they can be freed during cleanup
  our_os_pools[next_os_pool] = ptr;
  next_os_pool++;
  ptr = (void *) ((((unsigned long) ptr) + RM_PAGE_MASK) & ~RM_PAGE_MASK);
  sbrk_top = (char *) ptr + size;
  return ptr;
  }
  else if (size < 0)
  {
  // we don't currently support shrink behavior
  return (void *) MORECORE_FAILURE;
  }
  else
  {
  return sbrk_top;
  }
  }

  // cleanup any allocated memory pools
  // called as last thing before shutting down driver

  void osCleanupMem(void)
  {
  void **ptr;

  for (ptr = our_os_pools; ptr < &our_os_pools[MAX_POOL_ENTRIES]; ptr++)
  if (*ptr)
  {
  PoolDeallocate(*ptr);
  * ptr = 0;
  }
  }

*/


/* Helper code.  */

extern char **__libc_argv attribute_hidden;

static void
malloc_printerr (int action, const char *str, void *ptr, mstate ar_ptr)
{
  /* Avoid using this arena in future.  We do not attempt to synchronize this
     with anything else because we minimally want to ensure that __libc_message
     gets its resources safely without stumbling on the current corruption.  */
  if (ar_ptr)
    set_arena_corrupt (ar_ptr);

  if ((action & 5) == 5)
    __libc_message (action & 2, "%s\n", str);
  else if (action & 1)
    {
      char buf[2 * sizeof (uintptr_t) + 1];

      buf[sizeof (buf) - 1] = '\0';
      char *cp = _itoa_word ((uintptr_t) ptr, &buf[sizeof (buf) - 1], 16, 0);
      while (cp > buf)
        *--cp = '0';

      __libc_message (action & 2, "*** Error in `%s': %s: 0x%s ***\n",
                      __libc_argv[0] ? : "<unknown>", str, cp);
    }
  else if (action & 2)
    abort ();
}

/* We need a wrapper function for one of the additions of POSIX.  */
int
__posix_memalign (void **memptr, size_t alignment, size_t size)
{
  void *mem;

  /* Test whether the SIZE argument is valid.  It must be a power of
     two multiple of sizeof (void *).  */
  if (alignment % sizeof (void *) != 0
      || !powerof2 (alignment / sizeof (void *))
      || alignment == 0)
    return EINVAL;


  void *address = RETURN_ADDRESS (0);
  mem = _mid_memalign (alignment, size, address);

  if (mem != NULL)
    {
      *memptr = mem;
      return 0;
    }

  return ENOMEM;
}
weak_alias (__posix_memalign, posix_memalign)


int
__malloc_info (int options, FILE *fp)
{
  /* For now, at least.  */
  if (options != 0)
    return EINVAL;

  int n = 0;
  size_t total_nblocks = 0;
  size_t total_nfastblocks = 0;
  size_t total_avail = 0;
  size_t total_fastavail = 0;
  size_t total_system = 0;
  size_t total_max_system = 0;
  size_t total_aspace = 0;
  size_t total_aspace_mprotect = 0;



  if (__malloc_initialized < 0)
    ptmalloc_init ();

  fputs ("<malloc version=\"1\">\n", fp);

  /* Iterate over all arenas currently in use.  */
  mstate ar_ptr = &main_arena;
  do
    {
      fprintf (fp, "<heap nr=\"%d\">\n<sizes>\n", n++);

      size_t nblocks = 0;
      size_t nfastblocks = 0;
      size_t avail = 0;
      size_t fastavail = 0;
      struct
      {
        size_t from;
        size_t to;
        size_t total;
        size_t count;
      } sizes[NFASTBINS + NBINS - 1];
#define nsizes (sizeof (sizes) / sizeof (sizes[0]))

      LOCK_ARENA(ar_ptr, MALLOC_INFO_SITE);

      for (size_t i = 0; i < NFASTBINS; ++i)
        {
          chunkinfoptr _md_p = fastbin (ar_ptr, i);
          if (_md_p != NULL)
            {
              size_t nthissize = 0;
              size_t thissize = chunksize (_md_p);

              while (_md_p != NULL)
                {
                  ++nthissize;
                  _md_p = _md_p->fd;
                }

              fastavail += nthissize * thissize;
              nfastblocks += nthissize;
              sizes[i].from = thissize - (MALLOC_ALIGNMENT - 1);
              sizes[i].to = thissize;
              sizes[i].count = nthissize;
            }
          else
            sizes[i].from = sizes[i].to = sizes[i].count = 0;

          sizes[i].total = sizes[i].count * sizes[i].to;
        }


      mbinptr bin;
      chunkinfoptr _md_r;

      for (size_t i = 1; i < NBINS; ++i)
        {
          bin = bin_at (ar_ptr, i);
          _md_r = bin->fd;
          sizes[NFASTBINS - 1 + i].from = ~((size_t) 0);
          sizes[NFASTBINS - 1 + i].to = sizes[NFASTBINS - 1 + i].total
            = sizes[NFASTBINS - 1 + i].count = 0;

          if (_md_r != NULL)
            while (_md_r != bin)
              {
                ++sizes[NFASTBINS - 1 + i].count;
                sizes[NFASTBINS - 1 + i].total += _md_r->size;
                sizes[NFASTBINS - 1 + i].from
                  = MIN (sizes[NFASTBINS - 1 + i].from, _md_r->size);
                sizes[NFASTBINS - 1 + i].to = MAX (sizes[NFASTBINS - 1 + i].to,
                                                   _md_r->size);

                _md_r = _md_r->fd;
              }

          if (sizes[NFASTBINS - 1 + i].count == 0)
            sizes[NFASTBINS - 1 + i].from = 0;
          nblocks += sizes[NFASTBINS - 1 + i].count;
          avail += sizes[NFASTBINS - 1 + i].total;
        }

      UNLOCK_ARENA(ar_ptr, MALLOC_INFO_SITE);

      total_nfastblocks += nfastblocks;
      total_fastavail += fastavail;

      total_nblocks += nblocks;
      total_avail += avail;

      for (size_t i = 0; i < nsizes; ++i)
        if (sizes[i].count != 0 && i != NFASTBINS)
          fprintf (fp, "                                                              \
  <size from=\"%zu\" to=\"%zu\" total=\"%zu\" count=\"%zu\"/>\n",
                   sizes[i].from, sizes[i].to, sizes[i].total, sizes[i].count);

      if (sizes[NFASTBINS].count != 0)
        fprintf (fp, "\
  <unsorted from=\"%zu\" to=\"%zu\" total=\"%zu\" count=\"%zu\"/>\n",
                 sizes[NFASTBINS].from, sizes[NFASTBINS].to,
                 sizes[NFASTBINS].total, sizes[NFASTBINS].count);

      total_system += ar_ptr->system_mem;
      total_max_system += ar_ptr->max_system_mem;

      fprintf (fp,
               "</sizes>\n<total type=\"fast\" count=\"%zu\" size=\"%zu\"/>\n"
               "<total type=\"rest\" count=\"%zu\" size=\"%zu\"/>\n"
               "<system type=\"current\" size=\"%zu\"/>\n"
               "<system type=\"max\" size=\"%zu\"/>\n",
               nfastblocks, fastavail, nblocks, avail,
               ar_ptr->system_mem, ar_ptr->max_system_mem);

      if (ar_ptr != &main_arena)
        {
	  mchunkptr topchunk = chunkinfo2chunk(ar_ptr->_md_top);
          heap_info *heap = heap_for_ptr (topchunk);
          fprintf (fp,
                   "<aspace type=\"total\" size=\"%zu\"/>\n"
                   "<aspace type=\"mprotect\" size=\"%zu\"/>\n",
                   heap->size, heap->mprotect_size);
          total_aspace += heap->size;
          total_aspace_mprotect += heap->mprotect_size;
        }
      else
        {
          fprintf (fp,
                   "<aspace type=\"total\" size=\"%zu\"/>\n"
                   "<aspace type=\"mprotect\" size=\"%zu\"/>\n",
                   ar_ptr->system_mem, ar_ptr->system_mem);
          total_aspace += ar_ptr->system_mem;
          total_aspace_mprotect += ar_ptr->system_mem;
        }

      fputs ("</heap>\n", fp);
      ar_ptr = ar_ptr->next;
    }
  while (ar_ptr != &main_arena);

  fprintf (fp,
           "<total type=\"fast\" count=\"%zu\" size=\"%zu\"/>\n"
           "<total type=\"rest\" count=\"%zu\" size=\"%zu\"/>\n"
           "<total type=\"mmap\" count=\"%d\" size=\"%zu\"/>\n"
           "<system type=\"current\" size=\"%zu\"/>\n"
           "<system type=\"max\" size=\"%zu\"/>\n"
           "<aspace type=\"total\" size=\"%zu\"/>\n"
           "<aspace type=\"mprotect\" size=\"%zu\"/>\n"
           "</malloc>\n",
           total_nfastblocks, total_fastavail, total_nblocks, total_avail,
           mp_.n_mmaps, mp_.mmapped_mem,
           total_system, total_max_system,
           total_aspace, total_aspace_mprotect);

  return 0;
}
weak_alias (__malloc_info, malloc_info)


strong_alias (__libc_calloc, __calloc) weak_alias (__libc_calloc, calloc)
strong_alias (__libc_free, __cfree) weak_alias (__libc_free, cfree)
strong_alias (__libc_free, __free) strong_alias (__libc_free, free)
strong_alias (__libc_malloc, __malloc) strong_alias (__libc_malloc, malloc)
strong_alias (__libc_memalign, __memalign)
weak_alias (__libc_memalign, memalign)
strong_alias (__libc_realloc, __realloc) strong_alias (__libc_realloc, realloc)
strong_alias (__libc_valloc, __valloc) weak_alias (__libc_valloc, valloc)
strong_alias (__libc_pvalloc, __pvalloc) weak_alias (__libc_pvalloc, pvalloc)
strong_alias (__libc_mallinfo, __mallinfo)
weak_alias (__libc_mallinfo, mallinfo)
strong_alias (__libc_mallopt, __mallopt) weak_alias (__libc_mallopt, mallopt)

weak_alias (__malloc_stats, malloc_stats)
weak_alias (__malloc_usable_size, malloc_usable_size)
weak_alias (__malloc_trim, malloc_trim)
weak_alias (__malloc_get_state, malloc_get_state)
weak_alias (__malloc_set_state, malloc_set_state)


/* ------------------------------------------------------------
   History:

   [see ftp://g.oswego.edu/pub/misc/malloc.c for the history of dlmalloc]

*/
/*
 * Local variables:
 * c-basic-offset: 2
 * End:
 */
