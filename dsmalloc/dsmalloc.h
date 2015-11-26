#ifndef _DSMALLOC_H
#define _DSMALLOC_H

/* The following preprocessor macros are tested, 
 *   and hence should have #define directives:
 *
 *   HAVE_CONFIG_H    Define to #include "config.h" (autoconf-generated)
 *
 *   HAVE_UNISTD_H    Define to #include <unistd.h>
 *
 *   HAVE_SYS_UIO_H   Define to #include <sys/uio.h> (for writev)
 *   HAVE_WRITEV      Define if the 'writev' function is available
 *
 *   HAVE_SYS_PARAM_H Define to #include <sys/param.h> (for pagesize)
 *
 *   HAVE_MALLOC_H    Define to #include <malloc.h> (for struct mallinfo)
 *
 *   HAVE_FCNTL_H     Define to #include <fcntl.h>
 *
 *   HAVE_SYS_MMAN_H  Define to #include <sys/mman.h>
 *   HAVE_MMAP        Define if the 'mmap' function is available.
 *
 *   HAVE_SCHED_H     Define to #include <sched.h>
 *   HAVE_SCHED_YIELD Define id the 'sched_yield' function is available
 */

#include "dsassert.h"

/*
  __STD_C should be nonzero if using ANSI-standard C compiler, a C++
  compiler, or a C compiler sufficiently close to ANSI to get away
  with it.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


/*
  Define HAVE_MREMAP to make realloc() use mremap() to re-allocate
  large blocks.  This is currently only possible on Linux with
  kernel versions newer than 1.3.77.
*/

#ifndef HAVE_MREMAP
#ifdef linux
#define HAVE_MREMAP 1
#define _GNU_SOURCE
#else
#define HAVE_MREMAP 0
#endif
#endif /* HAVE_MREMAP */



#ifndef __STD_C
#if defined(__STDC__) || defined(_cplusplus)
#define __STD_C     1
#else
#define __STD_C     0
#endif 
#endif /*__STD_C*/


/*
  Void_t* is the pointer type that malloc should say it returns
*/

#ifndef Void_t
#if (__STD_C || defined(WIN32))
#define Void_t      void
#else
#define Void_t      char
#endif
#endif /*Void_t*/

#if __STD_C
#include <stddef.h>   /* for size_t */
#else
#include <sys/types.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* define HAVE_UNISTD_H if your system has a <unistd.h>. */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include <stdio.h>    /* needed for malloc_stats */
#include <errno.h>    /* needed for optional MALLOC_FAILURE_ACTION */

#include <string.h>
#include <stdlib.h>

#include <sys/resource.h>

extern int errno;

  /* 0: lazy, 
   * 1: medium (assertions compiled in), 
   * 2: high (guard pages at end of hash table and ciregions) 
   * 3: paranoid (guards at end of each allocated chunk, check at free) 
   */
#ifndef PARANOIA
#define PARANOIA 9
#endif



#include <stdarg.h>

  /* define for debugging */
  /* #define DNMALLOC_DEBUG */ 

  /* Do some extra checks? */
  /* #define DNMALLOC_CHECKS */

  /*
    The unsigned integer type used for comparing any two chunk sizes.
    This should be at least as wide as size_t, but should not be signed.
  */

#ifndef CHUNK_SIZE_T
#define CHUNK_SIZE_T unsigned long
#endif

/* 
  The unsigned integer type used to hold addresses when they are are
  manipulated as integers. Except that it is not defined on all
  systems, intptr_t would suffice.
*/
#ifndef PTR_UINT
#define PTR_UINT unsigned long
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
  among INTERNAL_SIZE_Ts should cast them to CHUNK_SIZE_T, being
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
#define MALLOC_ALIGNMENT       (2 * SIZE_SZ)
#endif

/* The corresponding bit mask value */
#define MALLOC_ALIGN_MASK      (MALLOC_ALIGNMENT - 1)



/*
  REALLOC_ZERO_BYTES_FREES should be set if a call to
  realloc with zero bytes should be the same as a call to free.
  Some people think it should. Otherwise, since this malloc
  returns a unique pointer for malloc(0), so does realloc(p, 0).
*/

#define REALLOC_ZERO_BYTES_FREES

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


/*
  USE_DL_PREFIX will prefix all public routines with the string 'dl'.
  This is necessary when you only want to use this malloc in one part 
  of a program, using your regular system malloc elsewhere.
*/

/* #define USE_DL_PREFIX */


/*
  USE_MALLOC_LOCK causes wrapper functions to surround each
  callable routine with pthread mutex lock/unlock.

  USE_MALLOC_LOCK forces USE_PUBLIC_MALLOC_WRAPPERS to be defined
*/

/* #define USE_MALLOC_LOCK */


/*
  If USE_PUBLIC_MALLOC_WRAPPERS is defined, every public routine is
  actually a wrapper function that first calls MALLOC_PREACTION, then
  calls the internal routine, and follows it with
  MALLOC_POSTACTION. This is needed for locking, but you can also use
  this, without USE_MALLOC_LOCK, for purposes of interception,
  instrumentation, etc. It is a sad fact that using wrappers often
  noticeably degrades performance of malloc-intensive programs.
*/


#ifdef USE_MALLOC_LOCK
#define USE_PUBLIC_MALLOC_WRAPPERS
#else
/* #define USE_PUBLIC_MALLOC_WRAPPERS */
#endif


/* 
   Two-phase name translation.
   All of the actual routines are given mangled names.
   When wrappers are used, they become the public callable versions.
   When DL_PREFIX is used, the callable names are prefixed.
*/

#ifndef USE_PUBLIC_MALLOC_WRAPPERS
#define cALLOc      public_cALLOc
#define fREe        public_fREe
#define mALLOc      public_mALLOc
#define mEMALIGn    public_mEMALIGn
#define posix_mEMALIGn    public_posix_mEMALIGn
#define rEALLOc     public_rEALLOc
#define vALLOc      public_vALLOc
#define pVALLOc     public_pVALLOc
#define mALLINFo    public_mALLINFo
#define mALLOPt     public_mALLOPt
#define mTRIm       public_mTRIm
#define mSTATs      public_mSTATs
#define mUSABLe     public_mUSABLe
#endif

#ifdef USE_DL_PREFIX
#define public_cALLOc    dlcalloc
#define public_fREe      dlfree
#define public_mALLOc    dlmalloc
#define public_mEMALIGn  dlmemalign
#define public_posix_mEMALIGn  dlposix_memalign
#define public_rEALLOc   dlrealloc
#define public_vALLOc    dlvalloc
#define public_pVALLOc   dlpvalloc
#define public_mALLINFo  dlmallinfo
#define public_mALLOPt   dlmallopt
#define public_mTRIm     dlmalloc_trim
#define public_mSTATs    dlmalloc_stats
#define public_mUSABLe   dlmalloc_usable_size
#else /* USE_DL_PREFIX */
#define public_cALLOc    calloc
#define public_fREe      free
#define public_mALLOc    malloc
#define public_mEMALIGn  memalign
#define public_posix_mEMALIGn  posix_memalign
#define public_rEALLOc   realloc
#define public_vALLOc    valloc
#define public_pVALLOc   pvalloc
#define public_mALLINFo  mallinfo
#define public_mALLOPt   mallopt
#define public_mTRIm     malloc_trim
#define public_mSTATs    malloc_stats
#define public_mUSABLe   malloc_usable_size
#endif /* USE_DL_PREFIX */


/*
  HAVE_MEMCPY should be defined if you are not otherwise using
  ANSI STD C, but still have memcpy and memset in your C library
  and want to use them in calloc and realloc. Otherwise simple
  macro versions are defined below.

  USE_MEMCPY should be defined as 1 if you actually want to
  have memset and memcpy called. People report that the macro
  versions are faster than libc versions on some systems.
  
  Even if USE_MEMCPY is set to 1, loops to copy/clear small chunks
  (of <= 36 bytes) are manually unrolled in realloc and calloc.
*/

#ifndef HAVE_MEMCPY
#define HAVE_MEMCPY
#endif

#ifndef USE_MEMCPY
#ifdef HAVE_MEMCPY
#define USE_MEMCPY 1
#else
#define USE_MEMCPY 0
#endif
#endif


#if (__STD_C || defined(HAVE_MEMCPY))

#ifdef WIN32
/* On Win32 memset and memcpy are already declared in windows.h */
#else
#if __STD_C
#if !defined(DARWIN)
void* memset(void*, int, size_t);
void* memcpy(void*, const void*, size_t);
#endif
#else
Void_t* memset();
Void_t* memcpy();
#endif
#endif
#endif

/*
  MALLOC_FAILURE_ACTION is the action to take before "return 0" when
  malloc fails to be able to return memory, either because memory is
  exhausted or because of illegal arguments.
  
  By default, sets errno if running on STD_C platform, else does nothing.  
*/

#ifndef MALLOC_FAILURE_ACTION
#if __STD_C
#define MALLOC_FAILURE_ACTION \
   errno = ENOMEM;

#else
#define MALLOC_FAILURE_ACTION
#endif
#endif

/*
  MORECORE-related declarations. By default, rely on sbrk
*/


#if !defined(HAVE_UNISTD_H)
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
#if __STD_C
extern Void_t*     sbrk(ptrdiff_t);
#else
extern Void_t*     sbrk();
#endif
#endif
#endif

/*
  MORECORE_FAILURE is the value returned upon failure of MORECORE
  as well as mmap. Since it cannot be an otherwise valid memory address,
  and must reflect values of standard sys calls, you probably ought not
  try to redefine it.
*/

#ifndef MORECORE_FAILURE
#define MORECORE_FAILURE ((void*)(-1UL))
#endif

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


/*
  This malloc requires mmap for heap management data. It is an error
  if mmap is not available.

  Additionally, mmap will be used to satisfy large requests.
*/

#ifndef HAVE_MMAP
#  ifdef SAMHAIN
#    error mmap not available, use --disable-dnmalloc
#  else
#    error HAVE_MMAP not defined, has your operating system mmap?
#  endif
#endif

/* 
   Standard unix mmap using /dev/zero clears memory so calloc doesn't
   need to.
*/

#ifndef MMAP_CLEARS
#define MMAP_CLEARS 1
#endif


/* 
   MMAP_AS_MORECORE_SIZE is the minimum mmap size argument to use if
   sbrk fails, and mmap is used as a backup (which is done only if
   HAVE_MMAP).  The value must be a multiple of page size.  This
   backup strategy generally applies only when systems have "holes" in
   address space, so sbrk cannot perform contiguous expansion, but
   there is still space available on system.  On systems for which
   this is known to be useful (i.e. most linux kernels), this occurs
   only when programs allocate huge amounts of memory.  Between this,
   and the fact that mmap regions tend to be limited, the size should
   be large, to avoid too many mmap calls and thus avoid running out
   of kernel resources.
*/

#ifndef MMAP_AS_MORECORE_SIZE
#define MMAP_AS_MORECORE_SIZE (1024 * 1024)
#endif


/*
  The system page size. To the extent possible, this malloc manages
  memory from the system in page-size units.  Note that this value is
  cached during initialization into a field of malloc_state. So even
  if malloc_getpagesize is a function, it is only called once.

  The following mechanics for getpagesize were adapted from bsd/gnu
  getpagesize.h. If none of the system-probes here apply, a value of
  4096 is used, which should be OK: If they don't apply, then using
  the actual value probably doesn't impact performance.
*/


#ifndef malloc_getpagesize

#  ifdef _SC_PAGESIZE         /* some SVR4 systems omit an underscore */
#    ifndef _SC_PAGE_SIZE
#      define _SC_PAGE_SIZE _SC_PAGESIZE
#    endif
#  endif

#  ifdef _SC_PAGE_SIZE
#    define malloc_getpagesize sysconf(_SC_PAGE_SIZE)
#  else
#    if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
       extern size_t getpagesize();
#      define malloc_getpagesize getpagesize()
#    else
#      ifdef WIN32 /* use supplied emulation of getpagesize */
#        define malloc_getpagesize getpagesize() 
#      else
#        if defined(HAVE_SYS_PARAM_H)
#          include <sys/param.h>
#        endif
#        ifdef EXEC_PAGESIZE
#          define malloc_getpagesize EXEC_PAGESIZE
#        else
#          ifdef NBPG
#            ifndef CLSIZE
#              define malloc_getpagesize NBPG
#            else
#              define malloc_getpagesize (NBPG * CLSIZE)
#            endif
#          else
#            ifdef NBPC
#              define malloc_getpagesize NBPC
#            else
#              ifdef PAGESIZE
#                define malloc_getpagesize PAGESIZE
#              else /* just guess */
#                define malloc_getpagesize (4096) 
#              endif
#            endif
#          endif
#        endif
#      endif
#    endif
#  endif
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

  HAVE_MALLOC_H should be set if you have a
  /usr/include/malloc.h file that includes a declaration of struct
  mallinfo.  If so, it is included; else an SVID2/XPG2 compliant
  version is declared below.  These must be precisely the same for
  mallinfo() to work.  The original SVID version of this struct,
  defined on most systems with mallinfo, declares all fields as
  ints. But some others define as unsigned long. If your system
  defines the fields using a type of different width than listed here,
  you must #include your system version and #define
  HAVE_MALLOC_H.
*/

/* #define HAVE_MALLOC_H */

/* On *BSD, malloc.h is deprecated, and on some *BSD including 
 * it may actually raise an error.
 */
#if defined(HAVE_MALLOC_H) && !defined(__OpenBSD__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(DARWIN)
#include <malloc.h>
#else

/* SVID2/XPG mallinfo structure */

struct mallinfo {
  int arena;    /* non-mmapped space allocated from system */
  int ordblks;  /* number of free chunks */
  int smblks;   /* number of fastbin blocks */
  int hblks;    /* number of mmapped regions */
  int hblkhd;   /* space in mmapped regions */
  int usmblks;  /* maximum total allocated space */
  int fsmblks;  /* space available in freed fastbin blocks */
  int uordblks; /* total allocated space */
  int fordblks; /* total free space */
  int keepcost; /* top-most, releasable (via malloc_trim) space */
};

/*
  SVID/XPG defines four standard parameter numbers for mallopt,
  normally defined in malloc.h.  Only one of these (M_MXFAST) is used
  in this malloc. The others (M_NLBLKS, M_GRAIN, M_KEEP) don't apply,
  so setting them has no effect. But this malloc also supports other
  options in mallopt described below.
*/
#endif


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
#if __STD_C
  Void_t*  public_mALLOc(size_t) __attribute__((malloc)); 
#else
  Void_t*  public_mALLOc();
#endif

/*
  free(Void_t* p)
  Releases the chunk of memory pointed to by p, that had been previously
  allocated using malloc or a related routine such as realloc.
  It has no effect if p is null. It can have arbitrary (i.e., bad!)
  effects if p has already been freed.

  Unless disabled (using mallopt), freeing very large spaces will
  when possible, automatically trigger operations that give
  back unused memory to the system, thus reducing program footprint.
*/
#if __STD_C
void     public_fREe(Void_t*);
#else
void     public_fREe();
#endif

/*
  calloc(size_t n_elements, size_t element_size);
  Returns a pointer to n_elements * element_size bytes, with all locations
  set to zero.
*/
#if __STD_C
  Void_t*  public_cALLOc(size_t, size_t) __attribute_malloc__;
#else
Void_t*  public_cALLOc();
#endif

/*
  realloc(Void_t* p, size_t n)
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
#if __STD_C
Void_t*  public_rEALLOc(Void_t*, size_t) __attribute_malloc__;
#else
Void_t*  public_rEALLOc();
#endif

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
#if __STD_C
Void_t*  public_mEMALIGn(size_t, size_t) __attribute_malloc__;
#else
Void_t*  public_mEMALIGn();
#endif

/*
  posix_memalign(void** memptr, size_t alignment, size_t n);
  Sets *memptr to the address of a newly allocated chunk of n bytes, aligned
  in accord with the alignment argument. Returns 0 on success, otherwise
  an error (EINVAL for incorrect alignment, ENOMEM for out of memory).

  The alignment must be a power of two, and a multiple of sizeof(void *).
*/
#if __STD_C
int public_posix_mEMALIGn(Void_t**, size_t, size_t);
#else
int public_posix_mEMALIGn();
#endif

/*
  valloc(size_t n);
  Equivalent to memalign(pagesize, n), where pagesize is the page
  size of the system. If the pagesize is unknown, 4096 is used.
*/
#if __STD_C
Void_t*  public_vALLOc(size_t) __attribute_malloc__;
#else
Void_t*  public_vALLOc();
#endif



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
  M_TRIM_THRESHOLD -1         256*1024   any   (-1U disables trimming)
  M_TOP_PAD        -2         0          any  
  M_MMAP_THRESHOLD -3         256*1024   any   (or 0 if no MMAP support)
  M_MMAP_MAX       -4         65536      any   (0 disables use of mmap)
*/
#if __STD_C
int      public_mALLOPt(int, int);
#else
int      public_mALLOPt();
#endif


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
#if __STD_C
struct mallinfo public_mALLINFo(void);
#else
struct mallinfo public_mALLINFo();
#endif

/*
  pvalloc(size_t n);
  Equivalent to valloc(minimum-page-that-holds(n)), that is,
  round up n to nearest pagesize.
 */
#if __STD_C
Void_t*  public_pVALLOc(size_t) __attribute_malloc__;
#else
Void_t*  public_pVALLOc();
#endif

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
  rreturn 0.
*/
#if __STD_C
int      public_mTRIm(size_t);
#else
int      public_mTRIm();
#endif

/*
  malloc_usable_size(Void_t* p);

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
#if __STD_C
size_t   public_mUSABLe(Void_t*);
#else
size_t   public_mUSABLe();
#endif

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
#if __STD_C
void     public_mSTATs();
#else
void     public_mSTATs();
#endif

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
  to 16 4byte fields, or small strings representing words, tokens,
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
#define DEFAULT_MXFAST     64
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

  The trim value must be greater than page size to have any useful
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
#define DEFAULT_TRIM_THRESHOLD (256 * 1024)
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
*/

#define M_MMAP_THRESHOLD      -3

#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD (256 * 1024)
#endif

/*
  M_MMAP_MAX is the maximum number of requests to simultaneously
  service using mmap. This parameter exists because
. Some systems have a limited number of internal tables for
  use by mmap, and using more than a few of them may degrade
  performance.

  The default is set to a value that serves only as a safeguard.
  Setting to 0 disables use of mmap for servicing large requests.  If
  HAVE_MMAP is not set, the default value is 0, and attempts to set it
  to non-zero values in mallopt will fail.
*/

#define M_MMAP_MAX             -4

#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX       (65536)
#endif

#ifdef __cplusplus
};  /* end of extern "C" */
#endif

/* 
  ========================================================================
  To make a fully customizable malloc.h header file, cut everything
  above this line, put into file malloc.h, edit to suit, and #include it 
  on the next line, as well as in programs that use this malloc.
  ========================================================================
*/


#endif
