/* dnmalloc copyright can be found a the end of the file */
/* this is dsmalloc */

/* 
   dsmalloc is based on dnmalloc; which in turn is based on dlmalloc 2.7.2 (by Doug Lea (dl@cs.oswego.edu))
   dlmalloc was released as public domain and contained the following license:
   
   "This is a version (aka dlmalloc) of malloc/free/realloc written by
   Doug Lea and released to the public domain.  Use, modify, and
   redistribute this code without permission or acknowledgement in any
   way you wish.  Send questions, comments, complaints, performance
   data, etc to dl@cs.oswego.edu
   
   * VERSION 2.7.2 Sat Aug 17 09:07:30 2002  Doug Lea  (dl at gee)
   
   Note: There may be an updated version of this malloc obtainable at
   ftp://gee.cs.oswego.edu/pub/misc/malloc.c
   Check before installing!"
   
*/

#include "dsmalloc.h" 
#include "dsassert.h" 
#include "metadata.h" 

#include <stdbool.h>

#ifdef METADATA_CHECKS
static bool metadata_is_consistent(void);
#endif

/* --------------------- public wrappers ---------------------- */

#ifdef USE_PUBLIC_MALLOC_WRAPPERS

/* DL_STATIC used to make functions (deep down) consistent
 * with prototypes (otherwise the prototypes are static
 * with USE_PUBLIC_MALLOC_WRAPPERS, but the functions aren't).
 * The gcc compiler doesn't care, but the HP-UX compiler does.
 */
#define DL_STATIC static

/* Declare all routines as internal */
#if __STD_C
static Void_t*  mALLOc(size_t) __attribute_malloc__;
static void     fREe(Void_t*);
static Void_t*  rEALLOc(Void_t*, size_t) __attribute_malloc__;
static Void_t*  mEMALIGn(size_t, size_t) __attribute_malloc__;
static int      posix_mEMALIGn(Void_t**, size_t, size_t);
static Void_t*  vALLOc(size_t) __attribute_malloc__;
static Void_t*  pVALLOc(size_t) __attribute_malloc__;
static Void_t*  cALLOc(size_t, size_t) __attribute_malloc__;
static int      mTRIm(size_t);
static size_t   mUSABLe(Void_t*);
static void     mSTATs();
static int      mALLOPt(int, int);
static struct mallinfo mALLINFo(void);
#else
static Void_t*  mALLOc();
static void     fREe();
static Void_t*  rEALLOc();
static Void_t*  mEMALIGn();
static int      posix_mEMALIGn();
static Void_t*  vALLOc();
static Void_t*  pVALLOc();
static Void_t*  cALLOc();
static int      mTRIm();
static size_t   mUSABLe();
static void     mSTATs();
static int      mALLOPt();
static struct mallinfo mALLINFo();
#endif

/*
  MALLOC_PREACTION and MALLOC_POSTACTION should be
  defined to return 0 on success, and nonzero on failure.
  The return value of MALLOC_POSTACTION is currently ignored
  in wrapper functions since there is no reasonable default
  action to take on failure.
*/


#ifdef USE_MALLOC_LOCK

# ifdef WIN32

static int mALLOC_MUTEx;
#define MALLOC_PREACTION   slwait(&mALLOC_MUTEx)
#define MALLOC_POSTACTION  slrelease(&mALLOC_MUTEx)
int dnmalloc_pthread_init(void) { return 0; }

# elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)

#   if defined(__NetBSD__)
#include <reentrant.h>
extern int __isthreaded;
static mutex_t thread_lock = MUTEX_INITIALIZER;
#define _MALLOC_LOCK()   if (__isthreaded) mutex_lock(&thread_lock)
#define _MALLOC_UNLOCK() if (__isthreaded) mutex_unlock(&thread_lock)
void _malloc_prefork(void) {  _MALLOC_LOCK(); }
void _malloc_postfork(void) { _MALLOC_UNLOCK(); }
#   endif

#   if defined(__OpenBSD__)
extern int  __isthreaded;
void   _thread_malloc_lock(void);
void   _thread_malloc_unlock(void);
#define _MALLOC_LOCK()           if (__isthreaded) _thread_malloc_lock()
#define _MALLOC_UNLOCK()         if (__isthreaded) _thread_malloc_unlock()
#   endif

#   if defined(__FreeBSD__)
extern int      __isthreaded;
struct _spinlock {
	volatile long	access_lock;
	volatile long	lock_owner;
	volatile char	*fname;
	volatile int	lineno;
};
typedef struct _spinlock spinlock_t;
#define	_SPINLOCK_INITIALIZER	{ 0, 0, 0, 0 }
void	_spinlock(spinlock_t *);
void	_spinunlock(spinlock_t *);
/* # include "/usr/src/lib/libc/include/spinlock.h" */
static spinlock_t thread_lock   = _SPINLOCK_INITIALIZER;
spinlock_t *__malloc_lock       = &thread_lock;
#define _MALLOC_LOCK()           if (__isthreaded) _spinlock(&thread_lock)
#define _MALLOC_UNLOCK()         if (__isthreaded) _spinunlock(&thread_lock)
#   endif

/* Common for all three *BSD
 */
static int malloc_active = 0;
static int dnmalloc_mutex_lock()
{
  _MALLOC_LOCK();
  if (!malloc_active)
    {
      ++malloc_active;
      return 0;
    }
  assert(malloc_active == 0);
  _MALLOC_UNLOCK();
  errno = EDEADLK;
  return 1;
}
static int dnmalloc_mutex_unlock()
{
  --malloc_active;
  _MALLOC_UNLOCK();
  return 0;
}
#define MALLOC_PREACTION   dnmalloc_mutex_lock()
#define MALLOC_POSTACTION  dnmalloc_mutex_unlock()
int dnmalloc_pthread_init(void) { return 0; }

# else

/* Wrapping malloc with pthread_mutex_lock/pthread_mutex_unlock
 *
 * Works fine on linux (no malloc in pthread_mutex_lock)
 * Works with on HP-UX if initialized after entering main()
 */ 
#include <pthread.h>
static int malloc_active      = 0;
void dnmalloc_fork_prepare(void);
void dnmalloc_fork_parent(void);
void dnmalloc_fork_child(void);

#if !defined(__linux__)

static pthread_mutex_t mALLOC_MUTEx;
pthread_once_t dnmalloc_once_control = PTHREAD_ONCE_INIT;
static int dnmalloc_use_mutex = 0;
static void dnmalloc_pthread_init_int(void)
{
  pthread_mutexattr_t   mta;
  pthread_mutexattr_init(&mta);
  pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&(mALLOC_MUTEx), &mta);
  pthread_mutexattr_destroy(&mta);       
  pthread_atfork(dnmalloc_fork_prepare, 
		 dnmalloc_fork_parent,
		 dnmalloc_fork_child);
  dnmalloc_use_mutex = 1;
}
int dnmalloc_pthread_init(void)
{
  return pthread_once(&dnmalloc_once_control, dnmalloc_pthread_init_int);
}

#else

static pthread_mutex_t mALLOC_MUTEx = PTHREAD_MUTEX_INITIALIZER;
static int dnmalloc_use_mutex = 1;
int dnmalloc_pthread_init(void) { 
  return pthread_atfork(dnmalloc_fork_prepare, 
			dnmalloc_fork_parent,
			dnmalloc_fork_child); 
}
#endif /* !defined(__linux__) */

void dnmalloc_fork_prepare(void) { 
  if (dnmalloc_use_mutex) 
    pthread_mutex_lock(&mALLOC_MUTEx);
}
void dnmalloc_fork_parent(void) { 
  if (dnmalloc_use_mutex)
    pthread_mutex_unlock(&mALLOC_MUTEx); 
}
void dnmalloc_fork_child(void) { 
#ifdef __GLIBC__
  if (dnmalloc_use_mutex)
    pthread_mutex_init(&mALLOC_MUTEx, NULL); 
#else
  if (dnmalloc_use_mutex)
    pthread_mutex_unlock(&mALLOC_MUTEx); 
#endif
}
/* iam: make use of the MALLOC_PREACTION and MALLOC_POSTACTION hooks */
static int dnmalloc_mutex_lock(pthread_mutex_t *mutex)
{
#ifdef  METADATA_CHECKS
  (void) metadata_is_consistent();
#endif

  if (dnmalloc_use_mutex)
    {
      int rc = pthread_mutex_lock(mutex);
      if (rc == 0)
	{
	  if (!malloc_active)
	    {
	      ++malloc_active;
	      return 0;
	    }
	  assert(malloc_active == 0);
	  (void) pthread_mutex_unlock(mutex);
	  errno = EDEADLK;
	  return 1;
	}
      return rc;
    }
  return 0;
}
static int dnmalloc_mutex_unlock(pthread_mutex_t *mutex)
{

#ifdef  METADATA_CHECKS
  (void) metadata_is_consistent();
#endif

  if (dnmalloc_use_mutex)
    {
      --malloc_active;
      return pthread_mutex_unlock(mutex);
    }
  return 0;
}
# define MALLOC_PREACTION   dnmalloc_mutex_lock(&mALLOC_MUTEx)
# define MALLOC_POSTACTION  dnmalloc_mutex_unlock(&mALLOC_MUTEx)

# endif

#else

/* Substitute anything you like for these */

# define MALLOC_PREACTION   (0)
# define MALLOC_POSTACTION  (0)
int dnmalloc_pthread_init(void) { return 0; }

#endif /* USE_MALLOC_LOCK */

Void_t* public_mALLOc(size_t bytes) {
  Void_t* m;
  if (MALLOC_PREACTION == 0) {
    m = mALLOc(bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

void public_fREe(Void_t* m) {
  if (MALLOC_PREACTION == 0) {
    fREe(m);
    (void) MALLOC_POSTACTION;
  }
}

Void_t* public_rEALLOc(Void_t* m, size_t bytes) {
  if (MALLOC_PREACTION == 0) {
    m = rEALLOc(m, bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

Void_t* public_mEMALIGn(size_t alignment, size_t bytes) {
  Void_t* m;
  if (MALLOC_PREACTION == 0) {
    m = mEMALIGn(alignment, bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

int public_posix_mEMALIGn(Void_t**memptr, size_t alignment, size_t bytes) {
  int m, ret;
  if ((ret = MALLOC_PREACTION) == 0) {
    m = posix_mEMALIGn(memptr, alignment, bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return ret;
}

Void_t* public_vALLOc(size_t bytes) {
  Void_t* m;
  if (MALLOC_PREACTION == 0) {
    m = vALLOc(bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

Void_t* public_pVALLOc(size_t bytes) {
  Void_t* m;
  if (MALLOC_PREACTION == 0) {
    m = pVALLOc(bytes);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

Void_t* public_cALLOc(size_t n, size_t elem_size) {
  Void_t* m;
  if (MALLOC_PREACTION == 0) {
    m = cALLOc(n, elem_size);
    (void) MALLOC_POSTACTION;
    return m;
  }
  return 0;
}

int public_mTRIm(size_t s) {
  int result;
  if (MALLOC_PREACTION == 0) {
    result = mTRIm(s);
    (void) MALLOC_POSTACTION;
    return result;
  }
  return 0;
}

size_t public_mUSABLe(Void_t* m) {
  size_t result;
  if (MALLOC_PREACTION == 0) {
    result = mUSABLe(m);
    (void) MALLOC_POSTACTION;
    return result;
  }
  return 0;
}

void public_mSTATs() {
  if (MALLOC_PREACTION == 0) {
    mSTATs();
    (void) MALLOC_POSTACTION;
  }
}

struct mallinfo public_mALLINFo() {
  struct mallinfo m;
  if (MALLOC_PREACTION == 0) {
    m = mALLINFo();
    (void) MALLOC_POSTACTION;
    return m;
  } else {
    struct mallinfo nm = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    return nm;
  }
}

int public_mALLOPt(int p, int v) {
  int result;
  if (MALLOC_PREACTION == 0) {
    result = mALLOPt(p, v);
    (void) MALLOC_POSTACTION;
    return result;
  }
  return 0;
}

#else

int dnmalloc_pthread_init(void) { return 0; }
#define DL_STATIC

#endif /* USE_PUBLIC_MALLOC_WRAPPERS */



/* ------------- Optional versions of memcopy ---------------- */


#if USE_MEMCPY

/* 
  Note: memcpy is ONLY invoked with non-overlapping regions,
  so the (usually slower) memmove is not needed.
*/

#define MALLOC_COPY(dest, src, nbytes)  memcpy(dest, src, nbytes)
#define MALLOC_ZERO(dest, nbytes)       memset(dest, 0,   nbytes)

#else /* !USE_MEMCPY */

/* Use Duff's device for good zeroing/copying performance. */

#define MALLOC_ZERO(charp, nbytes)                                            \
do {                                                                          \
  INTERNAL_SIZE_T* mzp = (INTERNAL_SIZE_T*)(charp);                           \
  CHUNK_SIZE_T  mctmp = (nbytes)/sizeof(INTERNAL_SIZE_T);                     \
  long mcn;                                                                   \
  if (mctmp < 8) mcn = 0; else { mcn = (mctmp-1)/8; mctmp %= 8; }             \
  switch (mctmp) {                                                            \
    case 0: for(;;) { *mzp++ = 0;                                             \
    case 7:           *mzp++ = 0;                                             \
    case 6:           *mzp++ = 0;                                             \
    case 5:           *mzp++ = 0;                                             \
    case 4:           *mzp++ = 0;                                             \
    case 3:           *mzp++ = 0;                                             \
    case 2:           *mzp++ = 0;                                             \
    case 1:           *mzp++ = 0; if(mcn <= 0) break; mcn--; }                \
  }                                                                           \
} while(0)

#define MALLOC_COPY(dest,src,nbytes)                                          \
do {                                                                          \
  INTERNAL_SIZE_T* mcsrc = (INTERNAL_SIZE_T*) src;                            \
  INTERNAL_SIZE_T* mcdst = (INTERNAL_SIZE_T*) dest;                           \
  CHUNK_SIZE_T  mctmp = (nbytes)/sizeof(INTERNAL_SIZE_T);                     \
  long mcn;                                                                   \
  if (mctmp < 8) mcn = 0; else { mcn = (mctmp-1)/8; mctmp %= 8; }             \
  switch (mctmp) {                                                            \
    case 0: for(;;) { *mcdst++ = *mcsrc++;                                    \
    case 7:           *mcdst++ = *mcsrc++;                                    \
    case 6:           *mcdst++ = *mcsrc++;                                    \
    case 5:           *mcdst++ = *mcsrc++;                                    \
    case 4:           *mcdst++ = *mcsrc++;                                    \
    case 3:           *mcdst++ = *mcsrc++;                                    \
    case 2:           *mcdst++ = *mcsrc++;                                    \
    case 1:           *mcdst++ = *mcsrc++; if(mcn <= 0) break; mcn--; }       \
  }                                                                           \
} while(0)

#endif

/* ------------------ MMAP support ------------------  */

/* iam: cleaned up this mess */
#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_NORESERVE
# define MAP_NORESERVE 0
#endif

#define MMAP(addr, size, prot, flags) \
  (mmap((addr), (size), (prot), (flags)|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0))


/*
  -----------------------  Chunk representations -----------------------
*/

#include "metadata.h"


/*
  ---------- Size and alignment checks and conversions ----------
*/

/* conversion from malloc headers to user pointers, and back */
#define chunk(p) (p->chunk)

/* The smallest possible chunk */
#define MIN_CHUNK_SIZE        16

/* The smallest size we can malloc is an aligned minimal chunk */

#define MINSIZE  \
  (CHUNK_SIZE_T)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

/* Check if m has acceptable alignment */

#define aligned_OK(m)  (((PTR_UINT)((m)) & (MALLOC_ALIGN_MASK)) == 0)

#define GUARD_SIZE 4

/* 
   Check if a request is so large that it would wrap around zero when
   padded and aligned. To simplify some other code, the bound is made
   low enough so that adding MINSIZE will also not wrap around zero.

   Make it 4*MINSIZE.
*/

#define REQUEST_OUT_OF_RANGE(req)                                 \
  ((CHUNK_SIZE_T)(req) >=                                         \
   (CHUNK_SIZE_T)(INTERNAL_SIZE_T)(-4 * MINSIZE))    

/* pad request bytes into a usable size -- internal version */

#define request2size(req)                                         \
  (((req) + GUARD_SIZE + MALLOC_ALIGN_MASK >= MINSIZE)  ?         \
   ((req) + GUARD_SIZE + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK :\
   MINSIZE)

/*  Same, except also perform argument check */

#define checked_request2size(req, sz)                             \
  if (!REQUEST_OUT_OF_RANGE(req)) {                               \
    (sz) = request2size(req);                                     \
    assert((sz-req) >= GUARD_SIZE);                               \
  } else {                                                        \
    MALLOC_FAILURE_ACTION;                                        \
    return 0;                                                     \
  }

#if PARANOIA > 2
static char * guard_set_p;
static char * guard_set_q;

#define guard_set(guard, P, request, sz)			  \
  assert((sz-request) >= GUARD_SIZE);                             \
  guard_set_p = (char*)(chunk(P));                                \
  guard_set_p += request;                                         \
  guard_set_q = (char*)(guard);                                   \
  *guard_set_p = *guard_set_q; ++guard_set_p; ++guard_set_q;      \
  *guard_set_p = *guard_set_q; ++guard_set_p; ++guard_set_q;      \
  *guard_set_p = *guard_set_q; ++guard_set_p; ++guard_set_q;      \
  *guard_set_p = *guard_set_q;                                    \
  (P)->req = request
 
#define guard_check(guard, P)				          \
  assert(0 == memcmp((((char *)chunk(P))+(P)->req),(void*)(guard),GUARD_SIZE));

#else
#define guard_set(guard, P, request, sz) ((void)0)
#define guard_check(guard, P) ((void)0)
#endif /* PARANOIA > 2 */

/* dnmalloc forward declarations */

static char * dnmalloc_arc4random(void);
static void malloc_mmap_state(void);

static chunkinfoptr new_chunkinfoptr(void);  

static void hashtable_add (chunkinfoptr ci);
static void hashtable_remove (mchunkptr p);              
static void hashtable_skiprm (chunkinfoptr ci_orig, chunkinfoptr ci_todelete);
static chunkinfoptr hashtable_lookup (mchunkptr p);


static chunkinfoptr next_chunkinfo (chunkinfoptr ci);          
static chunkinfoptr prev_chunkinfo (chunkinfoptr ci);



/*
  --------------- Physical chunk operations ---------------
*/


/* size field is or'ed with PREV_INUSE when previous adjacent chunk in use */
#define PREV_INUSE 0x1

/* extract inuse bit of previous chunk */
#define prev_inuse(p)       ((p)->size & PREV_INUSE)

/* size field is or'ed with IS_MMAPPED if the chunk was obtained with mmap() */
#define IS_MMAPPED 0x2

/* check for mmap()'ed chunk */
#define chunk_is_mmapped(p) ((p)->size & IS_MMAPPED)


/* size field is or'ed when the chunk is in use */
#define INUSE 0x4

/* extract inuse bit of chunk */
#define inuse(p)       ((p)->size & INUSE)

/* 
  Bits to mask off when extracting size 

  Note: IS_MMAPPED is intentionally not masked off from size field in
  macros for which mmapped chunks should never be seen. This should
  cause helpful core dumps to occur if it is tried by accident by
  people extending or adapting this malloc.
*/
#define SIZE_BITS (PREV_INUSE|IS_MMAPPED|INUSE)

/* Bits to mask off when extracting size of chunks for macros which do not use mmap */
#define SIZE_NOMMAP (PREV_INUSE|INUSE)

/* Get size, ignoring use bits */
#define chunksize(p)         ((p)->size & ~(SIZE_BITS))

/* Ptr to chunkinfo of next physical malloc_chunk. */
#define next_chunk(p) ((mchunkptr)( ((char*)(p)) + ((p)->size & SIZE_NOMMAP) ))

/* Treat space at ptr + offset as a chunk */
#define chunk_at_offset(p, s)  ((mchunkptr)(((char*)(p)) + (s)))

/* set/clear chunk as being inuse without otherwise disturbing */
#define set_inuse(p) ((p)->size |= INUSE)

#define clear_inuse(p) ((p)->size &= ~(INUSE))

#define set_previnuse(p) ((p)->size |= PREV_INUSE)

#define clear_previnuse(p) ((p)->size &= ~(PREV_INUSE))

static void set_previnuse_next (chunkinfoptr p)
{
   chunkinfoptr q;
   q = next_chunkinfo (p);
   if (q)
      set_previnuse (q);
}

#define set_all_inuse(p) \
set_inuse(p); \
set_previnuse_next(p);


/* Set size at head, without disturbing its use bit */
#define set_head_size(p, s)  ((p)->size = (((p)->size & SIZE_NOMMAP) | (s)))

/* Set size/use field */
#define set_head(p, s)       ((p)->size = (s))

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

typedef struct chunkinfo* mbinptr;

/* addressing -- note that bin_at(0) does not exist */
#define bin_at(m, i) (&(m)->bins[i])

/* analog of ++bin */
#define next_bin(b)  (b+1)

/* Reminders about list directionality within bins */
#define first(b)     ((b)->fd)
#define last(b)      ((b)->bk)

/* Take a chunk off a bin list */
#define unlink(P, BK, FD) {                                            \
  FD = P->fd;                                                          \
  BK = P->bk;                                                          \
  FD->bk = BK;                                                         \
  BK->fd = FD;                                                         \
}

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

    The bins top out around 1MB because we expect to service large
    requests via mmap.
*/

#define NBINS              96
#define NSMALLBINS         32
#define SMALLBIN_WIDTH      8
#define MIN_LARGE_SIZE    256

#define in_smallbin_range(sz)  \
  ((CHUNK_SIZE_T)(sz) < (CHUNK_SIZE_T)MIN_LARGE_SIZE)

#define smallbin_index(sz)     (((unsigned)(sz)) >> 3)

/*
  Compute index for size. We expect this to be inlined when
  compiled with optimization, else not, which works out well.
*/
static int largebin_index(size_t sz) {

  unsigned long  xx = sz >> SMALLBIN_WIDTH; 

  if (xx < 0x10000) 
    {
      unsigned int  m;           /* bit position of highest set bit of m */
      
      /* On intel, use BSRL instruction to find highest bit */
#if defined(__GNUC__) && defined(i386) && !defined(USE_UNO)

      unsigned int  x = (unsigned int) xx;

      __asm__("bsrl %1,%0\n\t"
	      : "=r" (m) 
	      : "rm"  (x));

#elif defined(__GNUC__) && defined(x86_64) && !defined(USE_UNO)

      __asm__("bsrq %1,%0\n\t"
              : "=r" (m)
              : "rm"  (xx));

#else

      /* Taken from Bit Twiddling Hacks
       * http://graphics.stanford.edu/~seander/bithacks.html
       * public domain
       */
      unsigned int  v  = (unsigned int) xx;
      register unsigned int shift;
      
      m =     (v > 0xFFFF) << 4; v >>= m;
      shift = (v > 0xFF  ) << 3; v >>= shift; m |= shift;
      shift = (v > 0xF   ) << 2; v >>= shift; m |= shift;
      shift = (v > 0x3   ) << 1; v >>= shift; m |= shift;
      m |= (v >> 1);
      
#endif
      
      /* Use next 2 bits to create finer-granularity bins */
      return NSMALLBINS + (m << 2) + ((sz >> (m + 6)) & 3);
    }
  else
    {
      return NBINS-1;
    }
}

#define bin_index(sz) \
 ((in_smallbin_range(sz)) ? smallbin_index(sz) : largebin_index(sz))

/*
  FIRST_SORTED_BIN_SIZE is the chunk size corresponding to the
  first bin that is maintained in sorted order. This must
  be the smallest size corresponding to a given bin.

  Normally, this should be MIN_LARGE_SIZE. But you can weaken
  best fit guarantees to sometimes speed up malloc by increasing value.
  Doing this means that malloc may choose a chunk that is 
  non-best-fitting by up to the width of the bin.

  Some useful cutoff values:
      512 - all bins sorted
     2560 - leaves bins <=     64 bytes wide unsorted  
    12288 - leaves bins <=    512 bytes wide unsorted
    65536 - leaves bins <=   4096 bytes wide unsorted
   262144 - leaves bins <=  32768 bytes wide unsorted
       -1 - no bins sorted (not recommended!)
*/

/* #define FIRST_SORTED_BIN_SIZE 65536 */

/*          12288 1m59 1m58 1m58
 *           2560 1m56 1m59 1m57
 * MIN_LARGE_SIZE 2m01 1m56 1m57
 */
#ifdef SAMHAIN
#define FIRST_SORTED_BIN_SIZE 2560
#else
#define FIRST_SORTED_BIN_SIZE MIN_LARGE_SIZE
#endif

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
#define unsorted_chunks(M)          (bin_at(M, 1))

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
    need to do so when getting memory from system, so we make
    initial_top treat the bin as a legal but unusable chunk during the
    interval between initialization and the first call to
    sYSMALLOc. (This is somewhat delicate, since it relies on
    the 2 preceding words to be zero during this interval as well.)
*/

/* Conveniently, the unsorted bin can be used as dummy top on first call */
#define initial_top(M)              (unsorted_chunks(M))

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

#define idx2block(i)     ((i) >> BINMAPSHIFT)
#define idx2bit(i)       ((1U << ((i) & ((1U << BINMAPSHIFT)-1))))

#define mark_bin(m,i)    ((m)->binmap[idx2block(i)] |=  idx2bit(i))
#define unmark_bin(m,i)  ((m)->binmap[idx2block(i)] &= ~(idx2bit(i)))
#define get_binmap(m,i)  ((m)->binmap[idx2block(i)] &   idx2bit(i))

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

typedef struct chunkinfo* mfastbinptr;

/* offset 2 to use otherwise unindexable first 2 bins */
#define fastbin_index(sz)        ((((unsigned int)(sz)) >> 3) - 2)

/* The maximum fastbin request size we support */
#define MAX_FAST_SIZE     80

#define NFASTBINS  (fastbin_index(request2size(MAX_FAST_SIZE))+1)

/*
  FASTBIN_CONSOLIDATION_THRESHOLD is the size of a chunk in free()
  that triggers automatic consolidation of possibly-surrounding
  fastbin chunks. This is a heuristic, so the exact value should not
  matter too much. It is defined at half the default trim threshold as a
  compromise heuristic to only attempt consolidation if it is likely
  to lead to trimming. However, it is not dynamically tunable, since
  consolidation reduces fragmentation surrounding loarge chunks even 
  if trimming is not used.
*/

#define FASTBIN_CONSOLIDATION_THRESHOLD  \
  ((unsigned long)(DEFAULT_TRIM_THRESHOLD) >> 1)

/*
  Since the lowest 2 bits in max_fast don't matter in size comparisons, 
  they are used as flags.
*/

/*
  ANYCHUNKS_BIT held in max_fast indicates that there may be any
  freed chunks at all. It is set true when entering a chunk into any
  bin.
*/

#define ANYCHUNKS_BIT        (1U)

#define have_anychunks(M)     (((M)->max_fast &  ANYCHUNKS_BIT))
#define set_anychunks(M)      ((M)->max_fast |=  ANYCHUNKS_BIT)
#define clear_anychunks(M)    ((M)->max_fast &= ~ANYCHUNKS_BIT)

/*
  FASTCHUNKS_BIT held in max_fast indicates that there are probably
  some fastbin chunks. It is set true on entering a chunk into any
  fastbin, and cleared only in malloc_consolidate.
*/

#define FASTCHUNKS_BIT        (2U)

#define have_fastchunks(M)   (((M)->max_fast &  FASTCHUNKS_BIT))
#define set_fastchunks(M)    ((M)->max_fast |=  (FASTCHUNKS_BIT|ANYCHUNKS_BIT))
#define clear_fastchunks(M)  ((M)->max_fast &= ~(FASTCHUNKS_BIT))

/* 
   Set value of max_fast. 
   Use impossibly small value if 0.
*/

#define set_max_fast(M, s) \
  (M)->max_fast = (((s) == 0)? SMALLBIN_WIDTH: request2size(s)) | \
  ((M)->max_fast &  (FASTCHUNKS_BIT|ANYCHUNKS_BIT))

#define get_max_fast(M) \
  ((M)->max_fast & ~(FASTCHUNKS_BIT | ANYCHUNKS_BIT))


/*
  morecore_properties is a status word holding dynamically discovered
  or controlled properties of the morecore function
*/

#define MORECORE_CONTIGUOUS_BIT  (1U)

#define contiguous(M) \
        (((M)->morecore_properties &  MORECORE_CONTIGUOUS_BIT))
#define noncontiguous(M) \
        (((M)->morecore_properties &  MORECORE_CONTIGUOUS_BIT) == 0)
#define set_contiguous(M) \
        ((M)->morecore_properties |=  MORECORE_CONTIGUOUS_BIT)
#define set_noncontiguous(M) \
        ((M)->morecore_properties &= ~MORECORE_CONTIGUOUS_BIT)

#define MORECORE_32BIT_BIT  (2U)

#define morecore32bit(M) \
        (((M)->morecore_properties &  MORECORE_32BIT_BIT))
#define nonmorecore32bit(M) \
        (((M)->morecore_properties &  MORECORE_32BIT_BIT) == 0)
#define set_morecore32bit(M) \
        ((M)->morecore_properties |=  MORECORE_32BIT_BIT)
#define set_nonmorecore32bit(M) \
        ((M)->morecore_properties &= ~MORECORE_32BIT_BIT)



/* ----------------- dnmalloc -------------------- */

/*
   ----------- Internal state representation and initialization -----------
*/

struct malloc_state {

  /* The maximum chunk size to be eligible for fastbin */
  INTERNAL_SIZE_T  max_fast;   /* low 2 bits used as flags */

  /* Fastbins */
  mfastbinptr      fastbins[NFASTBINS];

  /* Base of the topmost chunk -- not otherwise kept in a bin */
  chunkinfoptr        top;

  /* The remainder from the most recent split of a small request */
  chunkinfoptr        last_remainder;

  /* Normal bins */
  struct chunkinfo bins[NBINS];

  /* Bitmap of bins. Trailing zero map handles cases of largest binned size */
  unsigned int     binmap[BINMAPSIZE+1];

  /* Tunable parameters */
  CHUNK_SIZE_T     trim_threshold;
  INTERNAL_SIZE_T  top_pad;
  INTERNAL_SIZE_T  mmap_threshold;

  /* Memory map support */
  int              n_mmaps;
  int              n_mmaps_max;
  int              max_n_mmaps;

  /* Cache malloc_getpagesize */
  unsigned int     pagesize;  

  /* Canary */
  char             guard_stored[GUARD_SIZE];

  /* Track properties of MORECORE */
  unsigned int     morecore_properties;

  /* Statistics */
  INTERNAL_SIZE_T  mmapped_mem;
  INTERNAL_SIZE_T  sbrked_mem;
  INTERNAL_SIZE_T  max_sbrked_mem;
  INTERNAL_SIZE_T  max_mmapped_mem;
  INTERNAL_SIZE_T  max_total_mem;

  /*
   * Flag: set true once the mstate is initialized
   * We use it instead of checking whether av->top == 0 to trigger initialization,
   * because we want to keep av->top to NULL until we really allocate it in sysmalloc.
   *
   * iam: 1/1/2016 replaced this with the av->max_fast == 0 test.
   * int initialized;
   *
   */

  /* pool memory for the metadata */
  memcxt_t memcxt;

  /* metadata */
  metadata_t htbl;      

};

typedef struct malloc_state *mstate;

/* 
   There is exactly one instance of this struct in this malloc.
   If you are adapting this malloc in a way that does NOT use a static
   malloc_state, you MUST explicitly zero-fill it before using. This
   malloc relies on the property that malloc_state is initialized to
   all zeroes (as is true of C statics).
*/

static struct malloc_state * av_ = NULL;  /* never directly referenced */

/*
   All uses of av_ are via get_malloc_state().
   At most one "call" to get_malloc_state is made per invocation of
   the public versions of malloc and free, but other routines
   that in turn invoke malloc and/or free may call more then once. 
   Also, it is called in check* routines if DEBUG is set.
*/

#define get_malloc_state() (av_)

/*
  Initialize a malloc_state struct.

  This is called only from within malloc_consolidate, which needs
  be called in the same contexts anyway.  It is never called directly
  outside of malloc_consolidate because some optimizing compilers try
  to inline it at all call points, which turns out not to be an
  optimization at all. (Inlining it in malloc_consolidate is fine though.)
*/

#if __STD_C
static void malloc_mmap_state(void)
#else
static void malloc_mmap_state()
#endif
{
  int mprot;
  unsigned long pagesize = malloc_getpagesize;
  size_t size = (sizeof(struct malloc_state) + pagesize - 1) & ~(pagesize - 1);

  void * foo = MMAP(0, size+(2*pagesize), PROT_READ|PROT_WRITE, MAP_PRIVATE);

#ifdef NDEBUG
   if (foo == MAP_FAILED) {
      fprintf (stderr, "Couldn't mmap struct malloc_state: %s\n", strerror (errno));
      abort ();
   }
#else
   if (foo == MAP_FAILED) {
     fprintf(stderr, "foo = %p, errno = %d\n", foo, errno);
     perror("malloc_mmap_state");
   }
   assert(foo != MAP_FAILED);
#endif

   mprot = mprotect(foo, pagesize, PROT_NONE);
#ifdef NDEBUG
   if (mprot == -1) {
     fprintf (stderr, "Couldn't mprotect first non-rw page for struct malloc_state: %s\n",
	      strerror (errno));
     abort ();
   }
#else
   assert(mprot != -1);
#endif

   av_ = (struct malloc_state *) ((char*)foo + pagesize);

   MALLOC_ZERO(av_, sizeof(struct malloc_state));

   mprot = mprotect((void*)((char*)foo + size + pagesize), (size_t) pagesize, PROT_NONE);
#ifdef NDEBUG
   if (mprot == -1) {
     fprintf (stderr, 
	      "Couldn't mprotect last non-rw page for struct malloc_state: %s\n",
	      strerror (errno));
     abort ();
   }
#else
   assert(mprot != -1);
#endif
}

#if __STD_C
static void malloc_init_state(mstate av)
#else
static void malloc_init_state(av) mstate av;
#endif
{
  int     i;
  mbinptr bin;

  void *  morecore_test = MORECORE(0);

  /* Test morecore function 
   */
  set_morecore32bit(av);

  if (morecore_test == MORECORE_FAILURE)
    {
      set_nonmorecore32bit(av);
    }

  
  /* Establish circular links for normal bins */
  for (i = 1; i < NBINS; ++i) { 
    bin = bin_at(av,i);
    bin->fd = bin->bk = bin;
  }

  av->top_pad        = DEFAULT_TOP_PAD;
  av->n_mmaps_max    = DEFAULT_MMAP_MAX;
  av->mmap_threshold = DEFAULT_MMAP_THRESHOLD;
  av->trim_threshold = DEFAULT_TRIM_THRESHOLD;

#if MORECORE_CONTIGUOUS
  set_contiguous(av);
#else
  set_noncontiguous(av);
#endif

  set_max_fast(av, DEFAULT_MXFAST);

  init_memcxt(&av->memcxt);

  if( ! init_metadata(&av->htbl, &av->memcxt)){
    abort();
  }

  //  av->top = NULL

  //BD: attempt to get things to work!
  av->top = allocate_chunkinfoptr(&av->htbl);
  av->top->chunk     = (char*) (MORECORE(0));
  av->top->size      = 0;
  set_previnuse(av->top);
  clear_inuse(av->top);

  if( ! metadata_add(&av->htbl, av->top)){
    abort();
  }

  av->pagesize       = malloc_getpagesize;

  memcpy(av->guard_stored, dnmalloc_arc4random(), GUARD_SIZE);

  /* iam: poof  av->initialized = true; */


#ifdef DNMALLOC_DEBUG
  fprintf(stderr, "malloc_init_state OK\n");
  fprintf(stderr, "first av->top: %p\n", av->top);
#endif


}

/* Get a free chunkinfo */
static chunkinfoptr
new_chunkinfoptr()
{
  mstate av = get_malloc_state();
  return allocate_chunkinfoptr(&(av->htbl));
}


static void
hashtable_add (chunkinfoptr ci)
{
  mstate av = get_malloc_state();
  if( ! metadata_add(&av->htbl, ci)){
    abort();
  }
  
}

static void
hashtable_remove (mchunkptr p) 
{
  mstate av = get_malloc_state();
  if( ! metadata_delete(&av->htbl, p)){
    abort();
  }

}

static void inline hashtable_remove_mmapped(mchunkptr p){
  hashtable_remove (p);
}

static void
hashtable_skiprm (chunkinfoptr ci_orig, chunkinfoptr ci_todelete)
{
  mstate av = get_malloc_state();
  if( ! metadata_delete(&av->htbl, ci_todelete->chunk)){
    abort();
  }
  
}


static chunkinfoptr
hashtable_lookup (mchunkptr p)
{
  mstate av = get_malloc_state();
  return metadata_lookup(&av->htbl, p);
}




/* 
   Other internal utilities operating on mstates
*/

#if __STD_C
static Void_t*  sYSMALLOc(INTERNAL_SIZE_T, mstate);
static int      sYSTRIm(size_t, mstate);
static void     malloc_consolidate(mstate);
#else
static Void_t*  sYSMALLOc();
static int      sYSTRIm();
static void     malloc_consolidate();
#endif

/* dnmalloc functions */
/* needs mstate so moved here */

static int is_next_chunk(chunkinfoptr oldp, chunkinfoptr newp) {
  mchunkptr nextp;
  nextp = (mchunkptr) (((char *) (oldp->chunk)) + chunksize (oldp));
  if (nextp == chunk(newp)){
    return 1;
  } else {
    return 0;
  }
}

static chunkinfoptr
next_chunkinfo (chunkinfoptr ci)
{
  /* this gets the chunkinfoptr of the chunk next to ci's chunk: ci->chunk. */
   mchunkptr nextp;
   mstate av;

   assert(!chunk_is_mmapped(ci));

   av = get_malloc_state();
   nextp = (mchunkptr) (((char *) (ci->chunk)) + chunksize (ci));

   if (nextp == av->top->chunk) {
     return av->top;
   } else {
     return hashtable_lookup (nextp);
   }
}




/* Get the chunkinfo of the physically previous chunk */
/* Since we disposed of prev_size, we need this function to find the previous */
/* Precondition: ci must not be mmapped and ci's prev_inuse bit must not be set. */
static chunkinfoptr
prev_chunkinfo (chunkinfoptr ci) { 
  chunkinfoptr prev;
  mchunkptr prevchunk;

  assert(!chunk_is_mmapped(ci));
  assert(!prev_inuse(ci));

  if (ci->prev_size == 0) {
    prev = NULL;
  } else {
    prevchunk = (mchunkptr) (((char *) (ci->chunk)) - (ci->prev_size));
    prev = hashtable_lookup (prevchunk);
  }
  return prev;
}


/*
 * Adjust the size of the successor's block of ci
 * - ci must be a freshly created chunk (after we split a free block victim)
 * - the next of victim is now the next of ci
 * - so we must adjust the prev_size of next
 */
static void adjust_next_prevsize(chunkinfoptr ci) {
  chunkinfoptr next;

  assert(!inuse(ci));
  next = next_chunkinfo(ci);
  if (next != NULL) {
    assert(!prev_inuse(next));
    next->prev_size = chunksize(ci);
  }
}

/*
  typedef bool (*chunck_check_t)(metadata_t* lhtbl, chunkinfoptr ci, chunkinfoptr top);

  extern bool forall_metadata(metadata_t* lhash, chunck_check_t checkfn, chunkinfoptr top);

*/

/*
 * BD: intuition (to be confirmed)
 *
 * We have three bits of metadata per chunk:
 * - inuse
 * - prev_inuse
 * - mmapped
 *
 * An mmapped chunk was allocated via mmapped. It's in use. It has no predecessor and no successor.
 * Also the prev_size field of mmapped chunks stores a correction constant (i.e., if mmap returns
 * x and x is not aligned then the chunk will start at x + correction such that x + correction
 * is aligned).
 *
 * For non-mmapped chunks:
 * - The prev_inuse bit is set for ci if either ci has no predecessor chunk or if ci's predecessor 
 *   chunk is inuse.
 * - In the original dlmalloc, there's no actual inuse bit for ci. To mark that ci is in use,
 *   some macros set the prev_inuse bit of ci's successor chunk.
 * - To properly coalesce chunks, the following invariant must hold:
 *    (not prev_inuse(ci) => prev_size(ci) = chunksize(ci's predecessor)
 *   In other words, ci has a predecessor and if the predecessor is free then ci's metata
 *   is correct (ci's prev_size field is correct).
 *
 * Other invariant: all free blocks are coalesced
 * - if ci is free (not inuse) then its predecessor, if any, is inuse and its successor, if any,
 *   is in use too.
 */
bool metadata_chunk_ok(metadata_t* lhtbl, chunkinfoptr ci, chunkinfoptr top) {
  chunkinfoptr previous, next;
  bool inuse_prev, prev_inuse, inuse;
  //  bool inuse, next_prev_inuse;
  if (chunk_is_mmapped(ci)) {
    // nothing to check (regarding next and prev)
    return true;
  }

  if (chunksize(ci) == 0) {
    // Can't do anything. We have prev == ci == next
    // This should happen only for the first av->top
    return true;
  }

  inuse = (inuse(ci) != 0); // inuse bit of ci

  // check whether ci and its predecessor have consistent metadata
  prev_inuse = (prev_inuse(ci) != 0);   // prev_inuse bit of ci
  if (!prev_inuse) {
    previous = prev_chunkinfo(ci);
    if (previous == NULL) {
      fprintf(stderr, "metadata_chunk_ok: invalid prev_inuse bit: ci = %p, prev_inuse(ci) = %d, previous = NULL\n", ci, prev_inuse);
      return false;
    }
    inuse_prev = (inuse(previous) != 0);  // inuse bit of previous
    if (inuse_prev) {
      // should be the false
      fprintf(stderr, "metadata_chunk_ok: invalid inuse bits: ci = %p, prev_inuse(ci) = %d, previous = %p, inuse(previous) = %d\n",
	      ci, prev_inuse, previous, inuse_prev);
      return false;
    }
    if (chunksize(previous) != ci->prev_size){
      // should be equal
      fprintf(stderr, "metadata_chunk_ok: size mismatch: ci = %p, ci->prev_size = %zu, previous = %p, chunksize(previous) = %zu (top = %p)\n",
	      ci, ci->prev_size, previous, chunksize(previous), top);
      return false;
    }
    if (!inuse) {
      // consecutive free blocks so we didn't coalesce properly
      fprintf(stderr, "metadata_chunk_ok: adjacent free blocks: ci = %p, ci->prev_size = %zu, previous = %p, chunksize(previous) = %zu (top = %p)\n",
	      ci, ci->prev_size, previous, chunksize(previous), top);
      return false;
    }
  }

  if (!inuse) {
    // make sure the next block is in use
    next = next_chunkinfo(ci);
    if (next != NULL && !inuse(next)) {
      // consecutive free blocks so we didn't coalesce properly
      fprintf(stderr, "metadata_chunk_ok: adjacent free blocks: ci = %p, chunksize(ci) = %zu, next = %p, (top = %p)\n",
	      ci, chunksize(ci), next, top);
      return false;
    }
  }

#if 0
  // NOT SURE ABOUT THIS
  // check whether ci and its successor have consistent metadata
  next = next_chunkinfo(ci);
  if (next != NULL) {
    inuse = (inuse(ci) != 0); // inuse bit of ci
    next_prev_inuse = (prev_inuse(next) != 0); // prev_inuse bit of next
    if (inuse != next_prev_inuse) {
      // should be the same
      fprintf(stderr, "metadata_chunk_ok: invalid inuse bits: ci = %p, next = %p, inuse(ci) = %d, prev_inuse(next) = %d\n",
	      ci, next, inuse, next_prev_inuse);
      return false;
    }
    if (chunksize(ci) != next->prev_size) {
      fprintf(stderr, "metadata_chunk_ok: top = %p, ci = %p, next = %p\n", top, ci, next);
      fprintf(stderr, "metadata_chunk_ok: ci->size = %zu next->prev_size = %zu\n", chunksize(ci), next->prev_size);
      return false;
    }
  }
#endif

  return true;
}

/* 
 * iam: this can get called prior to init_malloc_state because of the "clever"
 * way malloc_consolidate  is used to call it. :-(
 * hence the metadata_initialized hack.
 */

#ifdef  METADATA_CHECKS
static bool metadata_is_consistent(void){
  mstate av;

#if 0
  static unsigned counter = 0;
  fprintf(stderr, "Checking metadata (%u)\n", counter);
  counter ++;
#endif

  av = get_malloc_state();
  if (av == NULL || (av->max_fast == 0)){
    return true;
  }
  return forall_metadata(&(av->htbl), metadata_chunk_ok, av->top); 
}

#endif



/*
  Debugging support
  Dnmalloc broke dlmallocs debugging functions, should fix them some 
  time in the future, for now leave them undefined.
*/

#define check_chunk(P)
#define check_free_chunk(P)
#define check_inuse_chunk(P)
#define check_remalloced_chunk(P,N)
#define check_malloced_chunk(P,N)
#define check_malloc_state()


/* ----------- Routines dealing with system allocation -------------- */

/*
  sysmalloc handles malloc cases requiring more memory from the system.
  On entry, it is assumed that av->top does not have enough
  space to service request for nb bytes, thus requiring that av->top
  be extended or replaced.
*/

#if __STD_C
static Void_t* sYSMALLOc(INTERNAL_SIZE_T nb, mstate av)
#else
static Void_t* sYSMALLOc(nb, av) INTERNAL_SIZE_T nb; mstate av;
#endif
{
  chunkinfoptr    old_top;        /* incoming value of av->top */
  INTERNAL_SIZE_T old_size;       /* its size */
  char*           old_end;        /* its end address */

  long            size;           /* arg to first MORECORE or mmap call */
  char*           brk;            /* return value from MORECORE */

  long            correction;     /* arg to 2nd MORECORE call */
  char*           snd_brk;        /* 2nd return val */

  INTERNAL_SIZE_T front_misalign; /* unusable bytes at front of new space */
  INTERNAL_SIZE_T end_misalign;   /* partial page left at end of new space */
  char*           aligned_brk;    /* aligned offset into brk */

  chunkinfoptr    p;              /* the allocated/returned chunk */
  chunkinfoptr    remainder;      /* remainder from allocation */
  chunkinfoptr    fencepost;      /* fencepost */
  CHUNK_SIZE_T    remainder_size; /* its size */

  CHUNK_SIZE_T    sum;            /* for updating stats */

  size_t          pagemask  = av->pagesize - 1;

#ifdef DNMALLOC_DEBUG
  fprintf(stderr, "Enter sysmalloc\n");
#endif
  /*
    If there is space available in fastbins, consolidate and retry
    malloc from scratch rather than getting memory from system.  This
    can occur only if nb is in smallbin range so we didn't consolidate
    upon entry to malloc. It is much easier to handle this case here
    than in malloc proper.
  */


  if (have_fastchunks(av)) {
    assert(in_smallbin_range(nb));
    malloc_consolidate(av);
#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "Return sysmalloc have_fastchunks\n");
#endif
    return mALLOc(nb - MALLOC_ALIGN_MASK);
  }


  /*
    If have mmap, and the request size meets the mmap threshold, and
    the system supports mmap, and there are few enough currently
    allocated mmapped regions, try to directly map this request
    rather than expanding top.
  */

  if (UNLIKELY((CHUNK_SIZE_T)(nb) >= (CHUNK_SIZE_T)(av->mmap_threshold) &&
	       (av->n_mmaps < av->n_mmaps_max))) {

    char* mm;             /* return value from mmap call*/

    /*
      Round up size to nearest page.  For mmapped chunks, the overhead
      is one SIZE_SZ unit larger than for normal chunks, because there
      is no following chunk whose prev_size field could be used.
    */
    size = (nb + MALLOC_ALIGN_MASK + pagemask) & ~pagemask;

    /* Don't try if size wraps around 0 */
    if ((CHUNK_SIZE_T)(size) > (CHUNK_SIZE_T)(nb)) {
	    

      mm = (char*)(MMAP(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE));
      
      if (mm != (char*)(MORECORE_FAILURE)) {
        
        /*
          The offset to the start of the mmapped region is stored
          in the prev_size field of the chunk. This allows us to adjust
          returned start address to meet alignment requirements here 
          and in memalign(), and still be able to compute proper
          address argument for later munmap in free() and realloc().
        */

	/*       
	  BD: the previous comment comes from the original dlmalloc.
	  In dnmalloc, the correction is stored not in the prev_size field
	  but in the hash_next field. Not sure why since prev_size does
          exist in the metadata.

	  Also: it's likely that front_misalign is always zero (assuming mmap
	  returns a page-aligned address and MALLOC_ALIGN_MASK is a reasonable
          number.)
	*/

        front_misalign = (INTERNAL_SIZE_T) mm & MALLOC_ALIGN_MASK;
	p = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "new_chunkinfoptr 0: %p\n", p);
#endif
        if (front_misalign > 0) {
          correction = MALLOC_ALIGNMENT - front_misalign;
          p->chunk = (mchunkptr)(mm + correction);
	  p->prev_size = correction;
	  assert(size > correction);
          set_head(p, (size - correction) |INUSE|IS_MMAPPED);
        }
        else {
          p->chunk = (mchunkptr)mm;
	  p->prev_size = 0;
          set_head(p, size|INUSE|IS_MMAPPED);
        }
        hashtable_add(p);
        /* update statistics */
        
        if (++av->n_mmaps > av->max_n_mmaps) 
          av->max_n_mmaps = av->n_mmaps;
        
        sum = av->mmapped_mem += size;
        if (sum > (CHUNK_SIZE_T)(av->max_mmapped_mem)) 
          av->max_mmapped_mem = sum;
        sum += av->sbrked_mem;
        if (sum > (CHUNK_SIZE_T)(av->max_total_mem)) 
          av->max_total_mem = sum;

        check_chunk(p);
        
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Return mmapped (%lu, total %lu)\n", 
		size, (unsigned long)/* size_t */av->max_total_mem );
#endif
        return chunk(p);
      }
    }
  }

  /* Record incoming configuration of top */

  old_top  = av->top;
  old_size = chunksize(old_top);
  old_end  = (char*)(chunk_at_offset(chunk(old_top), old_size));

  brk = snd_brk = (char*)(MORECORE_FAILURE); 

  /* 
     If not the first time through, we require old_size to be
     at least MINSIZE and to have prev_inuse set.
  */

  /* assert((old_top == initial_top(av) && old_size == 0) || 
	 ((CHUNK_SIZE_T) (old_size) >= MINSIZE &&
	 prev_inuse(old_top))); */

  /* Precondition: not enough current space to satisfy nb request */
  assert((CHUNK_SIZE_T)(old_size) < (CHUNK_SIZE_T)(nb + MINSIZE));

  /* Precondition: all fastbins are consolidated */
  assert(!have_fastchunks(av));

  /* Request enough space for nb + pad + overhead */
  size = nb + av->top_pad + MINSIZE; // BD: could this overflow? Do we have an upper bound on nb?

  /*
    If contiguous, we can subtract out existing space that we hope to
    combine with new space. We add it back later only if
    we don't actually get contiguous space.
  */
  if (contiguous(av))
    size -= old_size;

  /*
    Round to a multiple of page size.
    If MORECORE is not contiguous, this ensures that we only call it
    with whole-page arguments.  And if MORECORE is contiguous and
    this is not first time through, this preserves page-alignment of
    previous calls. Otherwise, we correct to page-align below.
  */

  size = (size + pagemask) & ~pagemask;

  /*
    Don't try to call MORECORE if argument is so big as to appear
    negative. Note that since mmap takes size_t arg, it may succeed
    below even if we cannot call MORECORE.
  */
  if (size > 0 && morecore32bit(av)) 
    brk = (char*)(MORECORE(size));

  /*
    If have mmap, try using it as a backup when MORECORE fails or
    cannot be used. This is worth doing on systems that have "holes" in
    address space, so sbrk cannot extend to give contiguous space, but
    space is available elsewhere.  Note that we ignore mmap max count
    and threshold limits, since the space will not be used as a
    segregated mmap region.
  */
  if (brk != (char*)(MORECORE_FAILURE)) {
    av->sbrked_mem += size;
  }

  else {

#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "Morecore failure in sysmalloc\n");
#endif

    /* Cannot merge with old top, so add its size back in */
    if (contiguous(av))
      size = (size + old_size + pagemask) & ~pagemask;

    /* If we are relying on mmap as backup, then use larger units */
    if ((CHUNK_SIZE_T)(size) < (CHUNK_SIZE_T)(MMAP_AS_MORECORE_SIZE))
      size = MMAP_AS_MORECORE_SIZE;

    /* Don't try if size wraps around 0 */
    if ((CHUNK_SIZE_T)(size) > (CHUNK_SIZE_T)(nb)) {

#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "Try mmap in sysmalloc\n");
#endif
      brk = (char*)(MMAP(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE));
      
      if (brk != (char*)(MORECORE_FAILURE)) {
        
	av->mmapped_mem += size;
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Mmapped successfully in sysmalloc %p\n", brk);
#endif

        /* We do not need, and cannot use, another sbrk call to find end */
        snd_brk = brk + size;
        
        /* 
           Record that we no longer have a contiguous sbrk region. 
           After the first time mmap is used as backup, we do not
           ever rely on contiguous space since this could incorrectly
           bridge regions.
        */
        set_noncontiguous(av);
      }
    }
  }

  if (brk != (char*)(MORECORE_FAILURE)) {
#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "Success path %lu allocated, sbrked %lu\n", 
	    size, (unsigned long)av->sbrked_mem);
#endif
    /* av->sbrked_mem += size; moved up */

    /*
      If MORECORE extends previous space, we can likewise extend top size.
    */
    
    if (brk == old_end && snd_brk == (char*)(MORECORE_FAILURE)) {
      set_head(old_top, (size + old_size) | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "Previous space extended\n");
#endif
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
    
    else {
      front_misalign = 0;
      end_misalign = 0;
      correction = 0;
      aligned_brk = brk;

      /*
        If MORECORE returns an address lower than we have seen before,
        we know it isn't really contiguous.  This and some subsequent
        checks help cope with non-conforming MORECORE functions and
        the presence of "foreign" calls to MORECORE from outside of
        malloc or by other threads.  We cannot guarantee to detect
        these in all cases, but cope with the ones we do detect.
      */
      if (contiguous(av) && old_size != 0 && brk < old_end) {
        set_noncontiguous(av);
      }
      
      /* handle contiguous cases */
      if (contiguous(av)) { 

#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Handle contiguous cases\n");
#endif
        /* 
           We can tolerate forward non-contiguities here (usually due
           to foreign calls) but treat them as part of our space for
           stats reporting.
        */
        if (old_size != 0) 
          av->sbrked_mem += brk - old_end;
        
        /* Guarantee alignment of first new chunk made from this space */

        front_misalign = (INTERNAL_SIZE_T) brk & MALLOC_ALIGN_MASK;
        if (front_misalign > 0) {

          /*
            Skip over some bytes to arrive at an aligned position.
            We don't need to specially mark these wasted front bytes.
            They will never be accessed anyway because
            prev_inuse of av->top (and any chunk created from its start)
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
        end_misalign = (INTERNAL_SIZE_T)(brk + size + correction);
        correction += ((end_misalign + pagemask) & ~pagemask) - end_misalign;
        
        assert(correction >= 0);
        snd_brk = (char*)(MORECORE(correction));
        
        if (snd_brk == (char*)(MORECORE_FAILURE)) {
          /*
            If can't allocate correction, try to at least find out current
            brk.  It might be enough to proceed without failing.
          */
          correction = 0;
          snd_brk = (char*)(MORECORE(0));
        }
        else if (snd_brk < brk) {
          /*
            If the second call gives noncontiguous space even though
            it says it won't, the only course of action is to ignore
            results of second call, and conservatively estimate where
            the first call left us. Also set noncontiguous, so this
            won't happen again, leaving at most one hole.
            
            Note that this check is intrinsically incomplete.  Because
            MORECORE is allowed to give more space than we ask for,
            there is no reliable way to detect a noncontiguity
            producing a forward gap for the second call.
          */
          snd_brk = brk + size;
          correction = 0;
          set_noncontiguous(av);
        }

      }
      
      /* handle non-contiguous cases */
      else { 

#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Handle non-contiguous cases\n");
#endif

        /* MORECORE/mmap must correctly align */
        assert(aligned_OK(brk));
        
        /* Find out current end of memory */
        if (snd_brk == (char*)(MORECORE_FAILURE)) {
          snd_brk = (char*)(MORECORE(0));
          av->sbrked_mem += snd_brk - brk - size;
        }
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Sbrked now %lu\n", (unsigned long)av->sbrked_mem);
#endif
      }
      
      /* Adjust top based on results of second sbrk.
       *
       * If mmap() has been used as backup for failed morecore(),
       * we end up in this branch as well.
       */
      if (snd_brk != (char*)(MORECORE_FAILURE)) {
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Adjust top, correction %lu\n", correction);
#endif
        /* hashtable_remove(chunk(av->top)); *//* rw 19.05.2008 removed */
	av->top =  new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "new_chunkinfoptr 1: %p\n", av->top);
#endif
        av->top->chunk = (mchunkptr)aligned_brk;
	assert(snd_brk > aligned_brk + correction);
        set_head(av->top, (snd_brk - aligned_brk + correction) | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "Adjust top, top %p size %lu\n", 
		av->top, (unsigned long)chunksize(av->top));
#endif
        hashtable_add(av->top);
        av->sbrked_mem += correction;
     
        /*
          If not the first time through, we either have a
          gap due to foreign sbrk or a non-contiguous region.  Insert a
          double fencepost at old_top to prevent consolidation with space
          we don't own. These fenceposts are artificial chunks that are
          marked as inuse. Original dlmalloc had two of these but too 
          small to use. To ensure that the linked lists contain a maximum 
          of 8 elements we only use 1. Inuse is determined by the 
          current rather than the next chunk anyway.
        */
   
        if (old_size != 0) {
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "Shrink old_top to insert fenceposts\n");
#endif
          /* 
             Shrink old_top to insert fenceposts, keeping size a
             multiple of MALLOC_ALIGNMENT. We know there is at least
             enough space in old_top to do this.
          */
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "Adjust top, old_top %p old_size before %lu\n", 
		  old_top, (unsigned long)old_size);
#endif
          old_size = (old_size - 4*SIZE_SZ) & ~MALLOC_ALIGN_MASK;
          set_head(old_top, old_size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "Adjust top, old_size after %lu\n", 
		  (unsigned long)old_size);
#endif
          
          /*
            Note that the following assignments completely overwrite
            old_top when old_size was previously MINSIZE.  This is
            intentional. We need the fencepost, even if old_top otherwise gets
            lost.
          */
          /* dnmalloc, we need the fencepost to be 16 bytes, however since 
	     it's marked inuse it will never be coalesced 
	  */
          fencepost = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "new_chunkinfoptr 2: %p\n", fencepost);
#endif
          fencepost->chunk = (mchunkptr) chunk_at_offset(chunk(old_top), 
							 old_size);
          fencepost->size = 16|INUSE|PREV_INUSE;
          hashtable_add(fencepost);
          /* 
             If possible, release the rest, suppressing trimming.
          */
          if (old_size >= MINSIZE) {
            INTERNAL_SIZE_T tt = av->trim_threshold;
#ifdef DNMALLOC_DEBUG
	    fprintf(stderr, "Release\n");
#endif
            av->trim_threshold = (INTERNAL_SIZE_T)(-1);
	    set_head(old_top, old_size | PREV_INUSE | INUSE);
	    guard_set(av->guard_stored, old_top, 0, old_size);
            fREe(chunk(old_top));
            av->trim_threshold = tt;
#ifdef DNMALLOC_DEBUG
	    fprintf(stderr, "Release done\n");
#endif
          }

#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "Adjust top, size %lu\n", 
		  (unsigned long)chunksize(av->top));
#endif

        } /* fenceposts */
      } /* adjust top */
    } /* not extended previous region */
    
    /* Update statistics */ /* FIXME check this */
    sum = av->sbrked_mem;
    if (sum > (CHUNK_SIZE_T)(av->max_sbrked_mem))
      av->max_sbrked_mem = sum;
    
    sum += av->mmapped_mem;
    if (sum > (CHUNK_SIZE_T)(av->max_total_mem))
      av->max_total_mem = sum;

    check_malloc_state();
    
    /* finally, do the allocation */

    p = av->top;
    size = chunksize(p);
    
#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "Size: %lu  nb+MINSIZE: %lu\n", 
	    (CHUNK_SIZE_T)(size), (CHUNK_SIZE_T)(nb + MINSIZE));
#endif

    /* check that one of the above allocation paths succeeded */
    if ((CHUNK_SIZE_T)(size) >= (CHUNK_SIZE_T)(nb + MINSIZE)) {
      remainder_size = size - nb;
      remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "new_chunkinfoptr 3: %p\n", remainder);
#endif
      remainder->chunk = chunk_at_offset(chunk(p), nb);
      av->top = remainder;
      set_head(p, nb | PREV_INUSE | INUSE);
      set_head(remainder, remainder_size | PREV_INUSE);
      remainder->prev_size = nb; // BD: probably useless
      hashtable_add(remainder);
      check_malloced_chunk(p, nb);
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "Return any (total %lu)\n", 
	      (unsigned long)/* size_t */av->max_total_mem );
#endif
      return chunk(p);
    }

  }

#ifdef DNMALLOC_DEBUG
  fprintf(stderr, "Return failed (total %lu)\n", 
	  (unsigned long)/* size_t */av->max_total_mem );
#endif

  /* catch all failure paths */
  MALLOC_FAILURE_ACTION;
  return 0;
}




/*
  sYSTRIm is an inverse of sorts to sYSMALLOc.  It gives memory back
  to the system (via negative arguments to sbrk) if there is unused
  memory at the `high' end of the malloc pool. It is called
  automatically by free() when top space exceeds the trim
  threshold. It is also called by the public malloc_trim routine.  It
  returns 1 if it actually released any memory, else 0.
*/

#if __STD_C
static int sYSTRIm(size_t pad, mstate av)
#else
static int sYSTRIm(pad, av) size_t pad; mstate av;
#endif
{
  long  top_size;        /* Amount of top-most memory */
  long  extra;           /* Amount to release */
  long  released;        /* Amount actually released */
  char* current_brk;     /* address returned by pre-check sbrk call */
  char* new_brk;         /* address returned by post-check sbrk call */
  size_t pagesz;

  pagesz = av->pagesize;
  top_size = chunksize(av->top);
  
  /* Release in pagesize units, keeping at least one page */
  extra = ((top_size - pad - MINSIZE + (pagesz-1)) / pagesz - 1) * pagesz;
  
  if (extra > 0) {
    
    /*
      Only proceed if end of memory is where we last set it.
      This avoids problems if there were foreign sbrk calls.
    */
    current_brk = (char*)(MORECORE(0));
    if (current_brk == (char*)(av->top) + top_size) {
      
      /*
        Attempt to release memory. We ignore MORECORE return value,
        and instead call again to find out where new end of memory is.
        This avoids problems if first call releases less than we asked,
        of if failure somehow altered brk value. (We could still
        encounter problems if it altered brk in some very bad way,
        but the only thing we can do is adjust anyway, which will cause
        some downstream failure.)
      */
      
      MORECORE(-extra);
      new_brk = (char*)(MORECORE(0));
      
      if (new_brk != (char*)MORECORE_FAILURE) {
        released = (long)(current_brk - new_brk);
        
        if (released != 0) {
          /* Success. Adjust top. */
          av->sbrked_mem -= released;
	  assert(top_size > released);
          set_head(av->top, (top_size - released) | PREV_INUSE);
          check_malloc_state();
          return 1;
        }
      }
    }
  }
  return 0;
}

/*
  ------------------------------ malloc ------------------------------
*/


#if __STD_C
DL_STATIC Void_t* mALLOc(size_t bytes)
#else
DL_STATIC   Void_t* mALLOc(bytes) size_t bytes;
#endif
{
  mstate av = get_malloc_state();

  INTERNAL_SIZE_T nb;               /* normalized request size */
  unsigned int    idx;              /* associated bin index */
  mbinptr         bin;              /* associated bin */
  mfastbinptr*    fb;               /* associated fastbin */

  chunkinfoptr       victim;           /* inspected/selected chunk */
  INTERNAL_SIZE_T size;             /* its size */
  int             victim_index;     /* its bin index */

  chunkinfoptr       remainder;        /* remainder from a split */
  CHUNK_SIZE_T    remainder_size;   /* its size */

  unsigned int    block;            /* bit map traverser */
  unsigned int    bit;              /* bit map traverser */
  unsigned int    map;              /* current word of binmap */

  chunkinfoptr       fwd;              /* misc temp for linking */
  chunkinfoptr       bck;              /* misc temp for linking */
  
  Void_t*         retval;

  /* chunkinfoptr	  next; */
 

  /*
    Convert request size to internal form by adding SIZE_SZ bytes
    overhead plus possibly more to obtain necessary alignment and/or
    to obtain a size of at least MINSIZE, the smallest allocatable
    size. Also, checked_request2size traps (returning 0) request sizes
    that are so large that they wrap around zero when padded and
    aligned.
  */
#if defined(SH_CUTEST)
  extern int malloc_count;
  ++malloc_count;
#endif

  checked_request2size(bytes, nb);

  /*
    Bypass search if no frees yet
   */
  if (av && have_anychunks(av)) {
    goto av_initialized;
  }
  else {
    if (!av || av->max_fast == 0) { /* initialization check */
      malloc_consolidate(av);
      av = get_malloc_state();
    }
    goto use_top;
  }

 av_initialized:

  /*
    If the size qualifies as a fastbin, first check corresponding bin.
  */
  if ((CHUNK_SIZE_T)(nb) <= (CHUNK_SIZE_T)(av->max_fast)) {
    fb = &(av->fastbins[(fastbin_index(nb))]);
    if ( (victim = *fb) != 0) {
      *fb = victim->fd;
      check_remalloced_chunk(victim, nb);
      guard_set(av->guard_stored, victim, bytes, nb);
      return chunk(victim);
    }
  }

  /*
    If a small request, check regular bin.  Since these "smallbins"
    hold one size each, no searching within bins is necessary.
    (For a large request, we need to wait until unsorted chunks are
    processed to find best fit. But for small ones, fits are exact
    anyway, so we can check now, which is faster.)
  */

  if (in_smallbin_range(nb)) {
    idx = smallbin_index(nb);
    bin = bin_at(av,idx);

    if ((victim = last(bin)) != bin) {
      bck = victim->bk;
      bin->bk = bck;
      bck->fd = bin;

      set_all_inuse(victim);
            
      check_malloced_chunk(victim, nb);
      guard_set(av->guard_stored, victim, bytes, nb);
      return chunk(victim);
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

  else {
    idx = largebin_index(nb);
    if (have_fastchunks(av)) 
      malloc_consolidate(av);
  }

  /*
    Process recently freed or remaindered chunks, taking one only if
    it is exact fit, or, if this a small request, the chunk is remainder from
    the most recent non-exact fit.  Place other traversed chunks in
    bins.  Note that this step is the only place in any routine where
    chunks are placed in bins.
  */
    
  while ( (victim = unsorted_chunks(av)->bk) != unsorted_chunks(av)) {
    bck = victim->bk;
    size = chunksize(victim);
    
    /* 
       If a small request, try to use last remainder if it is the
       only chunk in unsorted bin.  This helps promote locality for
       runs of consecutive small requests. This is the only
       exception to best-fit, and applies only when there is
       no exact fit for a small chunk.
    */
    
    if (UNLIKELY(in_smallbin_range(nb) && 
		 bck == unsorted_chunks(av) &&
		 victim == av->last_remainder &&
		 (CHUNK_SIZE_T)(size) > (CHUNK_SIZE_T)(nb + MINSIZE))) {
      
      /* split and reattach remainder */
      remainder_size = size - nb;
      remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "new_chunkinfoptr 4: %p\n", remainder);
#endif
      remainder->chunk = chunk_at_offset(chunk(victim), nb);
      unsorted_chunks(av)->bk = unsorted_chunks(av)->fd = remainder;
      av->last_remainder = remainder; 
      remainder->bk = remainder->fd = unsorted_chunks(av);
      
      set_head(victim, nb | PREV_INUSE|INUSE);
      set_head(remainder, remainder_size | PREV_INUSE);
      //      remainder->prev_size = nb;
      adjust_next_prevsize(remainder);  // BD
      hashtable_add(remainder);

      check_malloced_chunk(victim, nb);
      guard_set(av->guard_stored, victim, bytes, nb);
      return chunk(victim);
    }
    
    /* remove from unsorted list */
    unsorted_chunks(av)->bk = bck;
    bck->fd = unsorted_chunks(av);
    
    /* Take now instead of binning if exact fit */
    
    if (UNLIKELY(size == nb)) {
      set_all_inuse(victim)
      check_malloced_chunk(victim, nb);
      guard_set(av->guard_stored, victim, bytes, nb);
      return chunk(victim);
    }
    
    /* place chunk in bin */
    
    if (in_smallbin_range(size)) {

      victim_index = smallbin_index(size);
      bck = bin_at(av, victim_index);
      fwd = bck->fd;
    }
    else {
      victim_index = largebin_index(size);
      bck = bin_at(av, victim_index);
      fwd = bck->fd;
      
      if (UNLIKELY(fwd != bck)) {
        /* if smaller than smallest, place first */
        if ((CHUNK_SIZE_T)(size) < (CHUNK_SIZE_T)(bck->bk->size)) {
          fwd = bck;
          bck = bck->bk;
        }
        else if ((CHUNK_SIZE_T)(size) >= 
                 (CHUNK_SIZE_T)(FIRST_SORTED_BIN_SIZE)) {
          
          /* maintain large bins in sorted order */
          size |= PREV_INUSE|INUSE; /* Or with inuse bits to speed comparisons */
          while ((CHUNK_SIZE_T)(size) < (CHUNK_SIZE_T)(fwd->size)) 
            fwd = fwd->fd;
          bck = fwd->bk;
        }
      }
    }

    mark_bin(av, victim_index);
    victim->bk = bck;
    victim->fd = fwd;
    fwd->bk = victim;
    bck->fd = victim;
  }
  
  /*
    If a large request, scan through the chunks of current bin to
    find one that fits.  (This will be the smallest that fits unless
    FIRST_SORTED_BIN_SIZE has been changed from default.)  This is
    the only step where an unbounded number of chunks might be
    scanned without doing anything useful with them. However the
    lists tend to be short.
  */

  if (!in_smallbin_range(nb)) {
    bin = bin_at(av, idx);
    
    victim = last(bin);

    if (UNLIKELY(victim != bin)) {

      do {
	size = chunksize(victim);
      
	if ((CHUNK_SIZE_T)(size) >= (CHUNK_SIZE_T)(nb)) {
	  remainder_size = size - nb;
	  unlink(victim, bck, fwd);
        
	  /* Split */
	  if (remainder_size >= MINSIZE) {
	    remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
	    fprintf(stderr, "new_chunkinfoptr 5: %p\n", remainder);
#endif
	    remainder->chunk = chunk_at_offset(chunk(victim), nb);
	    unsorted_chunks(av)->bk = unsorted_chunks(av)->fd = remainder;
	    remainder->bk = remainder->fd = unsorted_chunks(av);
	    set_head(victim, nb | PREV_INUSE | INUSE);
	    set_head(remainder, remainder_size | PREV_INUSE);
	    //	    remainder->prev_size = nb;
	    adjust_next_prevsize(remainder); // BD
	    hashtable_add(remainder);
	    check_malloced_chunk(victim, nb);
	    guard_set(av->guard_stored, victim, bytes, nb);
	    return chunk(victim);
	  } 
	  /* Exhaust */
	  else  {
	    set_all_inuse(victim);
	    check_malloced_chunk(victim, nb);
	    guard_set(av->guard_stored, victim, bytes, nb);
	    return chunk(victim);
	  }
	}
	victim = victim->bk;
      } while(victim != bin);
    }
  }

  /*
    Search for a chunk by scanning bins, starting with next largest
    bin. This search is strictly by best-fit; i.e., the smallest
    (with ties going to approximately the least recently used) chunk
    that fits is selected.
    
    The bitmap avoids needing to check that most blocks are nonempty.
  */
    

  ++idx;
  bin = bin_at(av,idx);
  block = idx2block(idx);
  map = av->binmap[block];
  bit = idx2bit(idx);
  
  for (;;) {
    
    /* Skip rest of block if there are no more set bits in this block.  */
    if (bit > map || bit == 0) {
      do {
        if (++block >= BINMAPSIZE)  /* out of bins */
          goto use_top;
      } while ( (map = av->binmap[block]) == 0);
      
      bin = bin_at(av, (block << BINMAPSHIFT));
      bit = 1;
    }
    
    /* Advance to bin with set bit. There must be one. */
    while ((bit & map) == 0) {
      bin = next_bin(bin);
      bit <<= 1;
      assert(bit != 0);
    }
    
    /* Inspect the bin. It is likely to be non-empty */
    victim = last(bin);
    
    /*  If a false alarm (empty bin), clear the bit. */
    if (victim == bin) {
      av->binmap[block] = map &= ~bit; /* Write through */
      bin = next_bin(bin);
      bit <<= 1;
    }
    
    else {
      size = chunksize(victim);
      
      /*  We know the first chunk in this bin is big enough to use. */
      assert((CHUNK_SIZE_T)(size) >= (CHUNK_SIZE_T)(nb));
      
      remainder_size = size - nb;
      
      /* unlink */
      bck = victim->bk;
      bin->bk = bck;
      bck->fd = bin;
      
      /* Split */
      if (remainder_size >= MINSIZE) {
        remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "new_chunkinfoptr 6: %p, next(victim) = %p\n", remainder, next_chunkinfo(victim));
#endif
        remainder->chunk = chunk_at_offset(chunk(victim), nb);
        
        unsorted_chunks(av)->bk = unsorted_chunks(av)->fd = remainder;
        remainder->bk = remainder->fd = unsorted_chunks(av);
        /* advertise as last remainder */
        if (in_smallbin_range(nb)) 
          av->last_remainder = remainder; 
        
        set_head(victim, nb | PREV_INUSE | INUSE);
        set_head(remainder, remainder_size | PREV_INUSE);
	//	remainder->prev_size = nb;
	adjust_next_prevsize(remainder); // BD
        hashtable_add(remainder);
        check_malloced_chunk(victim, nb);
	guard_set(av->guard_stored, victim, bytes, nb);
        return chunk(victim);
      }
      /* Exhaust */
      else {
        set_all_inuse(victim);
        check_malloced_chunk(victim, nb);
	guard_set(av->guard_stored, victim, bytes, nb);
        return chunk(victim);
      }
      
    }
  }

  use_top:
   

  /*
    If large enough, split off the chunk bordering the end of memory
    (held in av->top). Note that this is in accord with the best-fit
    search rule.  In effect, av->top is treated as larger (and thus
    less well fitting) than any other available chunk since it can
    be extended to be as large as necessary (up to system
    limitations).
    
    We require that av->top always exists (i.e., has size >=
    MINSIZE) after initialization, so if it would otherwise be
    exhuasted by current request, it is replenished. (The main
    reason for ensuring it exists is that we may need MINSIZE space
    to put in fenceposts in sysmalloc.)
  */
  
  victim = av->top;
  size = chunksize(victim);
  
  if ((CHUNK_SIZE_T)(size) >= (CHUNK_SIZE_T)(nb + MINSIZE)) {
    remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "new_chunkinfoptr 7: %p\n", remainder);
#endif
    remainder_size = size - nb;
    set_head(remainder, remainder_size | PREV_INUSE);
    //    remainder->prev_size = nb;  // FIX? from dnmalloc to make sure we always have prev_size(next) = size(current)
    //    adjust_next_prevsize(remainder); remainder is top so it has no successor

    remainder->chunk = chunk_at_offset(chunk(victim), nb);
    hashtable_add(remainder);
    av->top = remainder;
    set_head(victim, nb | PREV_INUSE | INUSE);
    check_malloced_chunk(victim, nb);
    guard_set(av->guard_stored, victim, bytes, nb);
    return chunk(victim);
  }
  
  /* 
     If no space in top, relay to handle system-dependent cases 
  */
  retval = sYSMALLOc(nb, av);
  if (retval) {
    victim = hashtable_lookup(retval);
    guard_set(av->guard_stored, victim, bytes, nb);
  }
  return retval;
}

/*
  ------------------------------ free ------------------------------
*/

#if __STD_C
DL_STATIC void fREe(Void_t* mem)
#else
DL_STATIC void fREe(mem) Void_t* mem;
#endif
{
  mstate av = get_malloc_state();

  chunkinfoptr       p;           /* chunk corresponding to mem */
  INTERNAL_SIZE_T size;        /* its size */
  mfastbinptr*    fb;          /* associated fastbin */
  chunkinfoptr       prevchunk;   /* previous physical chunk */
  chunkinfoptr       nextchunk;   /* next contiguous chunk */
  INTERNAL_SIZE_T nextsize;    /* its size */
  INTERNAL_SIZE_T prevsize;    /* size of previous contiguous chunk */
  chunkinfoptr       bck;         /* misc temp for linking */
  chunkinfoptr       fwd;         /* misc temp for linking */
  chunkinfoptr	     next;
#if defined(SH_CUTEST)
  extern int malloc_count;
  --malloc_count;
#endif

  /* free(0) has no effect */
  if (mem != 0) {
    p = hashtable_lookup(mem);
    /* check that memory is managed by us 
     * and is inuse 
     */
    if (UNLIKELY(!p || !inuse(p))) 
      {
#ifdef DNMALLOC_CHECKS
	if (p) {
	  fprintf(stderr, "Attempt to free memory not in use\n");
	  abort();
	} else {
	  fprintf(stderr, "Attempt to free memory not allocated\n");
	  abort();
	}
#endif
	assert(p && inuse(p));
	return;
      }

    guard_check(av->guard_stored, p);

    size = chunksize(p);

    check_inuse_chunk(p);

    /*
      If eligible, place chunk on a fastbin so it can be found
      and used quickly in malloc.
    */

    if ((CHUNK_SIZE_T)(size) <= (CHUNK_SIZE_T)(av->max_fast)

#if TRIM_FASTBINS
        /* 
           If TRIM_FASTBINS set, don't place chunks
           bordering top into fastbins
        */
        && (chunk_at_offset(chunk(p), size) != av->top)
#endif
        ) {

      set_fastchunks(av);
      fb = &(av->fastbins[fastbin_index(size)]);
      p->fd = *fb;
      *fb = p;
    }

    /*
       Consolidate other non-mmapped chunks as they arrive.
    */

    else if (!chunk_is_mmapped(p)) {
      set_anychunks(av);

      nextchunk = next_chunkinfo(p);
      if (nextchunk)
	nextsize = chunksize(nextchunk);
      else
	nextsize = 0;/* gcc doesn't notice that it's only used if (nextchunk)*/

      /* consolidate backward */
      if (UNLIKELY(!prev_inuse(p))) {
        prevchunk = prev_chunkinfo(p);
        prevsize = chunksize(prevchunk);
#ifdef DNMALLOC_CHECKS
	if (inuse(prevchunk)) {
		fprintf(stderr, "Dnmalloc error: trying to unlink an inuse chunk: %p (chunk: %p)\n This is definitely a bug, please report it to dnmalloc@fort-knox.org.\n", prevchunk, chunk(prevchunk));
		abort();
	}
#else
	assert(!inuse(prevchunk));
#endif
        size += prevsize;
        unlink(prevchunk, bck, fwd);
	set_head(p, size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "coalescing 1\n");
#endif
        hashtable_skiprm(prevchunk,p);
        p = prevchunk;
      }

      if (nextchunk) {
	if (nextchunk != av->top) {
	  /* get and clear inuse bit */
	  clear_previnuse(nextchunk);
	  
	  /* consolidate forward */
	  if (!inuse(nextchunk)) {
	    unlink(nextchunk, bck, fwd);
	    size += nextsize;
	    set_head(p, size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	    fprintf(stderr, "coalescing 2\n");
#endif
	    hashtable_skiprm(p, nextchunk);
	  }
	  
	  set_head(p, size | PREV_INUSE);
	  next = next_chunkinfo(p);
	  if (next)
	    next->prev_size = size;
	  
	  /*
	    Place the chunk in unsorted chunk list. Chunks are
	    not placed into regular bins until after they have
	    been given one chance to be used in malloc.
	  */
	  
	  bck = unsorted_chunks(av);
	  fwd = bck->fd;
	  p->bk = bck;
	  p->fd = fwd;
	  bck->fd = p;
	  fwd->bk = p;
	  
	  nextchunk = next_chunkinfo(p);
	  if (nextchunk)
	    nextchunk->prev_size = chunksize(p);	
	  
	  check_free_chunk(p);
	}
	
	/*
	  If the chunk borders the current high end of memory,
	  consolidate into top
	*/
	
	else {
	  size += nextsize;
	  set_head(p, size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "coalescing remove 1\n");
#endif
	  hashtable_remove(chunk(av->top));
	  av->top = p;
	  check_chunk(p);
	}
      } /* if (nextchunk) */

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

      if (UNLIKELY((CHUNK_SIZE_T)(size) >= FASTBIN_CONSOLIDATION_THRESHOLD)) { 
        if (have_fastchunks(av)) 
          malloc_consolidate(av);

#ifndef MORECORE_CANNOT_TRIM        
        if ((CHUNK_SIZE_T)(chunksize(av->top)) >= 
            (CHUNK_SIZE_T)(av->trim_threshold))
	  {
	    if (morecore32bit(av))
	      {
#ifdef DNMALLOC_DEBUG
		fprintf(stderr, "Calling systrim from free()\n");
#endif
		sYSTRIm(av->top_pad, av);
#ifdef DNMALLOC_DEBUG
		fprintf(stderr, "Systrim done\n");
#endif
	      }
	  }
#endif
      }

    }
    /*
      If the chunk was allocated via mmap, release via munmap()
      Note that if HAVE_MMAP is false but chunk_is_mmapped is
      true, then user must have overwritten memory. There's nothing
      we can do to catch this error unless DEBUG is set, in which case
      check_inuse_chunk (above) will have triggered error.
    */

    else {
      //      fprintf(stderr, "Puzzle number one\n");
      int ret;
      INTERNAL_SIZE_T offset = (INTERNAL_SIZE_T) p->prev_size;
      av->n_mmaps--;
      av->mmapped_mem -= (size + offset);
      ret = munmap((char*) chunk(p) - offset, size + offset);
      hashtable_remove_mmapped(chunk(p));
      // munmap returns non-zero on failure 
      assert(ret == 0);
      if(ret != 0){
	fprintf(stderr, "munmap returned %d\n", ret);
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

#if __STD_C
static void malloc_consolidate(mstate av)
#else
static void malloc_consolidate(av) mstate av;
#endif
{
  mfastbinptr*    fb;                 /* current fastbin being consolidated */
  mfastbinptr*    maxfb;              /* last fastbin (for loop control) */
  chunkinfoptr       p;                  /* current chunk being consolidated */
  chunkinfoptr       nextp;              /* next chunk to consolidate */
  chunkinfoptr       prevp;
  chunkinfoptr       unsorted_bin;       /* bin header */
  chunkinfoptr       first_unsorted;     /* chunk to link to */

  /* These have same use as in free() */
  chunkinfoptr       nextchunk;
  INTERNAL_SIZE_T size;
  INTERNAL_SIZE_T nextsize;
  INTERNAL_SIZE_T prevsize;
  chunkinfoptr       bck;
  chunkinfoptr       fwd;
  chunkinfoptr	     next;
 
  /*
    If max_fast is 0, we know that av hasn't
    yet been initialized, in which case do so below
  */
  if (av && av->max_fast != 0) {
    clear_fastchunks(av);

    unsorted_bin = unsorted_chunks(av);

    /*
      Remove each chunk from fast bin and consolidate it, placing it
      then in unsorted bin. Among other reasons for doing this,
      placing in unsorted bin avoids needing to calculate actual bins
      until malloc is sure that chunks aren't immediately going to be
      reused anyway.
    */
    
    maxfb = &(av->fastbins[fastbin_index(av->max_fast)]);
    fb = &(av->fastbins[0]);
    do {
      if ( UNLIKELY((p = *fb) != 0)) {
        *fb = 0;
	do {
          check_inuse_chunk(p);
          nextp = p->fd;
          
          /*
	   * Slightly streamlined version of consolidation code in free() 
	   */

          size = chunksize(p);
          nextchunk = next_chunkinfo(p);

	  /* gcc doesn't notice that it's only used if (nextchunk) */
	  if (nextchunk)
	    nextsize = chunksize(nextchunk);
	  else
	    nextsize = 0; 
          
	  if (!prev_inuse(p)) {
             prevp = prev_chunkinfo(p);
             prevsize = chunksize(prevp);
             size += prevsize;
#ifdef DNMALLOC_CHECKS
	     if (inuse(prevp)) {
		fprintf(stderr, "Dnmalloc error: trying to unlink an inuse chunk (2): %p (chunk: %p)\n This is definitely a bug, please report it to dnmalloc@fort-knox.org.\n", prevp, chunk(prevp));
		     abort();
	     }
#else
	     assert(!inuse(prevp));
#endif
             unlink(prevp, bck, fwd);
             set_head(p, size | PREV_INUSE);	     
#ifdef DNMALLOC_DEBUG
	     fprintf(stderr, "coalescing 3\n");
#endif
             hashtable_skiprm(prevp,p);
             p=prevp;
          }
          
	  if (nextchunk) {
	    if (nextchunk != av->top) {

	      clear_previnuse(nextchunk);
            
	      if (!inuse(nextchunk)) {
		size += nextsize;
		unlink(nextchunk, bck, fwd);
		set_head(p, size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
		fprintf(stderr, "coalescing 4\n");
#endif
		hashtable_skiprm(p,nextchunk);
	      }
	      
	      first_unsorted = unsorted_bin->fd;
	      unsorted_bin->fd = p;
	      first_unsorted->bk = p;
	      
	      set_head(p, size | PREV_INUSE);
	      p->bk = unsorted_bin;
	      p->fd = first_unsorted;
	      next = next_chunkinfo(p);
	      if (next)
	    	next->prev_size = size;

            
	    }
          
	    else {
	      size += nextsize;
	      set_head(p, size | PREV_INUSE);
#ifdef DNMALLOC_DEBUG
	      fprintf(stderr, "coalescing remove 2\n");
#endif
	      hashtable_remove(chunk(av->top));
	      av->top = p;
	    }
	  }
          
        } while ( (p = nextp) != 0);
        
      }
    } while (fb++ != maxfb);
  }
  else {
    // Initialize dnmalloc
    malloc_mmap_state();
    malloc_init_state(get_malloc_state());
    check_malloc_state();
  }
}

/*
  ------------------------------ realloc ------------------------------
*/


#if __STD_C
DL_STATIC Void_t* rEALLOc(Void_t* oldmem, size_t bytes)
#else
DL_STATIC Void_t* rEALLOc(oldmem, bytes) Void_t* oldmem; size_t bytes;
#endif
{
  mstate av = get_malloc_state();

  INTERNAL_SIZE_T  nb;              /* padded request size */

  chunkinfoptr        oldp;            /* chunk corresponding to oldmem */
  INTERNAL_SIZE_T  oldsize;         /* its size */

  chunkinfoptr        newp;            /* chunk to return */
  INTERNAL_SIZE_T  newsize;         /* its size */
  Void_t*          newmem;          /* corresponding user mem */

  chunkinfoptr        next;            /* next contiguous chunk after oldp */

  chunkinfoptr        remainder;       /* extra space at end of newp */
  CHUNK_SIZE_T     remainder_size;  /* its size */

  chunkinfoptr        bck;             /* misc temp for linking */
  chunkinfoptr        fwd;             /* misc temp for linking */

  CHUNK_SIZE_T     copysize;        /* bytes to copy */
  unsigned int     ncopies;         /* INTERNAL_SIZE_T words to copy */
  INTERNAL_SIZE_T* s;               /* copy source */ 
  INTERNAL_SIZE_T* d;               /* copy destination */

  
#ifdef REALLOC_ZERO_BYTES_FREES
  if (UNLIKELY(bytes == 0)) {
    fREe(oldmem);
    return 0;
  }
#endif

  if (UNLIKELY(!av || av->max_fast == 0)) {
    malloc_consolidate(av);
    av = get_malloc_state();
  }

  /* realloc of null is supposed to be same as malloc */
  if (UNLIKELY(oldmem == 0)) 
    return mALLOc(bytes);

  checked_request2size(bytes, nb);

  oldp    = hashtable_lookup(oldmem);
  
  if (UNLIKELY(!oldp || !inuse(oldp))){ 
     /* attempt to either realloc memory not managed by us 
      * or memory that is not in use 
      */
#ifdef DNMALLOC_CHECKS
    if (oldp) {
      fprintf(stderr, "Attempt to free memory not in use\n");
      abort();
    } else {
      fprintf(stderr, "Attempt to free memory not allocated\n");
      abort();
    }
#endif
    assert(oldp && inuse(oldp));
    return 0;     
  }

  guard_check(av->guard_stored, oldp);

  oldsize = chunksize(oldp);

  check_inuse_chunk(oldp);

  if (!chunk_is_mmapped(oldp)) {

    if (UNLIKELY((CHUNK_SIZE_T)(oldsize) >= (CHUNK_SIZE_T)(nb))) {
      /* already big enough; split below */
      newp    = oldp;
      newsize = oldsize;
    }

    else {
      next = next_chunkinfo(oldp);
      if (next)
      	next->prev_size = oldsize;
      /* Try to expand forward into top */
      if (next && next == av->top &&
          (CHUNK_SIZE_T)(newsize = oldsize + chunksize(next)) >=
          (CHUNK_SIZE_T)(nb + MINSIZE)) {
         set_head_size(oldp, nb);
#ifdef DNMALLOC_DEBUG
	 fprintf(stderr, "coalescing remove 3\n");
#endif
         hashtable_remove(chunk(av->top));
	 // BD: big bug here. We can't keep a pointer to a dead chunkinfo
	 // We must reallocate a new top.
	 av->top = new_chunkinfoptr();
         av->top->chunk = chunk_at_offset(chunk(oldp), nb);
	 assert(newsize > nb);
         set_head(av->top, (newsize - nb) | PREV_INUSE);
         /* av->top->chunk has been moved move in hashtable */
         hashtable_add(av->top);
	 guard_set(av->guard_stored, oldp, bytes, nb);
         return chunk(oldp);
      }
      
      /* Try to expand forward into next chunk;  split off remainder below */
      else if (next && next != av->top && 
               !inuse(next) &&
               (CHUNK_SIZE_T)(newsize = oldsize + chunksize(next)) >=
               (CHUNK_SIZE_T)(nb)) {
        newp = oldp;
        unlink(next, bck, fwd);
#ifdef DNMALLOC_DEBUG
	fprintf(stderr, "coalescing remove 4\n");
#endif
        hashtable_remove(chunk(next));
	next = next_chunkinfo(oldp);
	if (next)
	  next->prev_size = newsize;
      }

      /* allocate, copy, free */
      else {

        newmem = mALLOc(nb - MALLOC_ALIGN_MASK);
        if (newmem == 0)
          return 0; /* propagate failure */

        newp = hashtable_lookup(newmem);
        newsize = chunksize(newp);
	
        /* next = next_chunkinfo(oldp); *//* 'next' never used rw 19.05.2008 */
        /*
          Avoid copy if newp is next chunk after oldp.
        */
	if (UNLIKELY(is_next_chunk(oldp, newp))) {
	  newsize += oldsize;
	  set_head_size(oldp, newsize);
#ifdef DNMALLOC_DEBUG
	  fprintf(stderr, "coalescing 5\n");
#endif
	  hashtable_skiprm(oldp, newp);
          newp = oldp;
        }
        else {
          /*
            Unroll copy of <= 40 bytes (80 if 8byte sizes)
            We know that contents have an even number of
            INTERNAL_SIZE_T-sized words; minimally 4 (2 on amd64).
          */
          
          copysize = oldsize;
          s = (INTERNAL_SIZE_T*)(oldmem);
          d = (INTERNAL_SIZE_T*)(newmem);
          ncopies = copysize / sizeof(INTERNAL_SIZE_T);
          assert(ncopies >= 2);
          
          if (ncopies > 10)
            MALLOC_COPY(d, s, copysize);
          
          else {
            *(d+0) = *(s+0);
            *(d+1) = *(s+1);
	    if (ncopies > 2) {
	      *(d+2) = *(s+2);
	      *(d+3) = *(s+3);
	      if (ncopies > 4) {
		*(d+4) = *(s+4);
		*(d+5) = *(s+5);
		if (ncopies > 6) {
		  *(d+6) = *(s+6);
		  *(d+7) = *(s+7);
		  if (ncopies > 8) {
		    *(d+8) = *(s+8);
		    *(d+9) = *(s+9);
		  }
                }
              }
            }
          }
          
          fREe(oldmem);
          check_inuse_chunk(newp);
	  guard_set(av->guard_stored, newp, bytes, nb);
          return chunk(newp);
        }
      }
    }

    /* If possible, free extra space in old or extended chunk */

    assert((CHUNK_SIZE_T)(newsize) >= (CHUNK_SIZE_T)(nb));

    remainder_size = newsize - nb;

    if (remainder_size >= MINSIZE) { /* split remainder */
      remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "new_chunkinfoptr 8: %p\n", remainder);
#endif
      remainder->chunk = chunk_at_offset(chunk(newp), nb);
      set_head_size(newp, nb);
      set_head(remainder, remainder_size | PREV_INUSE | INUSE);
      set_head(remainder, remainder_size | PREV_INUSE);
      // BD: we don't want to call adjust_next_prevsize here
      //      remainder->prev_size = nb;
      //      adjust_next_prevsize(remainder);

      hashtable_add(remainder);
      /* Mark remainder as inuse so free() won't complain */
      set_all_inuse(remainder);
      guard_set(av->guard_stored, remainder, 0, remainder_size);
      fREe(chunk(remainder)); 
    }
    else { /* not enough extra to split off */
      set_head_size(newp, newsize);
      set_all_inuse(newp);
    }

    check_inuse_chunk(newp);
    guard_set(av->guard_stored, newp, bytes, nb);
    return chunk(newp);
  }

  /*
    Handle mmap cases
  */

  else {
//iam: anothe puzzle
#if 0 //HAVE_MREMAP
    INTERNAL_SIZE_T offset = (INTERNAL_SIZE_T) oldp->hash_next;
    size_t pagemask = av->pagesize - 1;
    char *cp;
    CHUNK_SIZE_T  sum;
    
    /* Note the extra SIZE_SZ overhead */
    //newsize = (nb + offset + SIZE_SZ + pagemask) & ~pagemask;
    newsize = (nb + offset + pagemask) & ~pagemask;

    /* don't need to remap if still within same page */
    if (oldsize == newsize - offset)
      {
	guard_set(av->guard_stored, oldp, bytes, nb);
	return oldmem;
      }

    cp = (char*)mremap((char*)chunk(oldp) - offset, oldsize + offset, newsize, 1);
    
    if (cp != (char*)MORECORE_FAILURE) {
       
      hashtable_remove_mmapped(chunk(oldp));
       
      oldp->chunk = (mchunkptr)(cp + offset);
      assert(newsize > offset);
      set_head(oldp, (newsize - offset)|IS_MMAPPED|INUSE);
      
      hashtable_add(oldp);
      
      assert(aligned_OK(chunk(oldp))); /* rw fix: newp -> oldp */
      assert(( ((INTERNAL_SIZE_T) oldp->hash_next) == offset));
      
      /* update statistics */
      sum = av->mmapped_mem += newsize - oldsize;
      if (sum > (CHUNK_SIZE_T)(av->max_mmapped_mem)) 
        av->max_mmapped_mem = sum;
      sum += av->sbrked_mem;
      if (sum > (CHUNK_SIZE_T)(av->max_total_mem)) 
        av->max_total_mem = sum;
      
      guard_set(av->guard_stored, oldp, bytes, nb);
      return chunk(oldp);
    }
#endif /* have MREMAP */

    /* Note the extra SIZE_SZ overhead. */
    if ((CHUNK_SIZE_T)(oldsize) >= (CHUNK_SIZE_T)(nb + SIZE_SZ)) 
      newmem = oldmem; /* do nothing */
    else {
      /* Must alloc, copy, free. */
      newmem = mALLOc(nb - MALLOC_ALIGN_MASK);
      if (newmem != 0) {
        MALLOC_COPY(newmem, oldmem, oldsize);
        fREe(oldmem);
      }
    }
    guard_set(av->guard_stored, hashtable_lookup(newmem), bytes, nb);
    return newmem;
  }
}

/*
  ---------------------------posix_memalign ----------------------------
*/

#if __STD_C
DL_STATIC int posix_mEMALIGn(Void_t** memptr, size_t alignment, size_t bytes)
#else
DL_STATIC int posix_mEMALIGn(memptr, alignment, bytes) Void_t** memptr; size_t alignment; size_t bytes;
#endif
{
  mstate av;

  if (alignment % sizeof(void *) != 0)
    return EINVAL;
  if ((alignment & (alignment - 1)) != 0)
    return EINVAL;

  av = get_malloc_state();
  if (!av || av->max_fast == 0) malloc_consolidate(av);
  *memptr =  mEMALIGn(alignment, bytes);

  return (*memptr != NULL ? 0 : ENOMEM);
}

/*
  ------------------------------ memalign ------------------------------
*/

#if __STD_C
DL_STATIC Void_t* mEMALIGn(size_t alignment, size_t bytes)
#else
DL_STATIC Void_t* mEMALIGn(alignment, bytes) size_t alignment; size_t bytes;
#endif
{
  INTERNAL_SIZE_T nb;             /* padded  request size */
  char*           m;              /* memory returned by malloc call */
  chunkinfoptr       p;              /* corresponding chunk */
  char*           brk;            /* alignment point within p */
  chunkinfoptr       newp;           /* chunk to return */
  INTERNAL_SIZE_T newsize;        /* its size */
  INTERNAL_SIZE_T leadsize;       /* leading space before alignment point */
  chunkinfoptr       remainder;      /* spare room at end to split off */
  CHUNK_SIZE_T    remainder_size; /* its size */
  INTERNAL_SIZE_T size;

#ifndef NDEBUG

  mstate          av;
  av = get_malloc_state();

#endif

  /* If need less alignment than we give anyway, just relay to malloc */

  if (UNLIKELY(alignment <= MALLOC_ALIGNMENT)) return mALLOc(bytes);

  /* Otherwise, ensure that it is at least a minimum chunk size */

  if (alignment <  MINSIZE) alignment = MINSIZE;

  /* Make sure alignment is power of 2 (in case MINSIZE is not).  */
  if (UNLIKELY((alignment & (alignment - 1)) != 0)) {
    size_t a = MALLOC_ALIGNMENT * 2;
    while ((CHUNK_SIZE_T)a < (CHUNK_SIZE_T)alignment) a <<= 1;
    alignment = a;
  }

  checked_request2size(bytes, nb);

  /*
    Strategy: find a spot within that chunk that meets the alignment
    request, and then possibly free the leading and trailing space.
  */


  /* Call malloc with worst case padding to hit alignment. */

  m  = (char*)(mALLOc(nb + alignment + MINSIZE));

  if (m == 0) return 0; /* propagate failure */


  p = hashtable_lookup((mchunkptr) m);

  if ((((PTR_UINT)(m)) % alignment) != 0) { /* misaligned */

    /*
      Find an aligned spot inside chunk.  Since we need to give back
      leading space in a chunk of at least MINSIZE, if the first
      calculation places us at a spot with less than MINSIZE leader,
      we can move to the next aligned spot -- we've allocated enough
      total room so that this is always possible.
    */

    brk = (char*) ((PTR_UINT)(((PTR_UINT)(m + alignment - 1)) &
                           -((signed long) alignment)));
    if ((CHUNK_SIZE_T)(brk - (char*)(chunk(p))) < MINSIZE)
      brk += alignment;

    newp = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
    fprintf(stderr, "new_chunkinfoptr 9: %p\n", newp);
#endif
    newp->chunk = (mchunkptr)brk;
    leadsize = brk - (char*)(chunk(p));
    newsize = chunksize(p) - leadsize;

    /* For mmapped chunks, just adjust offset */
    if (UNLIKELY(chunk_is_mmapped(p))) {
      newp->prev_size = p->prev_size + leadsize;
      set_head(newp, newsize|IS_MMAPPED|INUSE);
      hashtable_remove_mmapped(chunk(p));
      hashtable_add(newp);
      guard_set(av->guard_stored, newp, bytes, nb);
      return chunk(newp);
    }

    /* Otherwise, give back leader, use the rest */
    set_head(newp, newsize | PREV_INUSE | INUSE);
    set_head_size(p, leadsize);
    newp->prev_size = leadsize;
    set_all_inuse(newp);
    hashtable_add(newp); /* 20.05.2008 rw */
    guard_set(av->guard_stored, p, 0, leadsize);
    fREe(chunk(p));
    p = newp;

    assert (newsize >= nb &&
            (((PTR_UINT)(chunk(p))) % alignment) == 0);
  }

  /* Also give back spare room at the end */
  if (!chunk_is_mmapped(p)) {
    size = chunksize(p);
    if ((CHUNK_SIZE_T)(size) > (CHUNK_SIZE_T)(nb + MINSIZE)) {
      remainder = new_chunkinfoptr();
#ifdef DNMALLOC_DEBUG
      fprintf(stderr, "new_chunkinfoptr 10: %p\n", remainder);
#endif
      remainder_size = size - nb;
      remainder->chunk = chunk_at_offset(chunk(p), nb);
      set_head(remainder, remainder_size | PREV_INUSE | INUSE);
      //      remainder->prev_size = nb;
      adjust_next_prevsize(remainder); // BD

      set_head_size(p, nb);
      hashtable_add(remainder); /* 20.05.2008 rw */
      guard_set(av->guard_stored, remainder, 0, remainder_size);
      fREe(chunk(remainder));
    }
  }

  check_inuse_chunk(p);
  guard_set(av->guard_stored, p, bytes, nb);
  return chunk(p);
}

/*
  ------------------------------ calloc ------------------------------
*/

#if __STD_C
DL_STATIC Void_t* cALLOc(size_t n_elements, size_t elem_size)
#else
DL_STATIC Void_t* cALLOc(n_elements, elem_size) size_t n_elements; size_t elem_size;
#endif
{
  chunkinfoptr p;
  CHUNK_SIZE_T  clearsize;
  CHUNK_SIZE_T  nclears;
  INTERNAL_SIZE_T* d;
  Void_t* mem;
 
  
  mem = mALLOc(n_elements * elem_size);

  if (mem != 0) {
    p = hashtable_lookup(mem);

    if (!chunk_is_mmapped(p))
    {  
      /*
        Unroll clear of <= 40 bytes (80 if 8byte sizes)
        We know that contents have an even number of
        INTERNAL_SIZE_T-sized words; minimally 4 (2 on amd64).
      */

      d = (INTERNAL_SIZE_T*)mem;
      clearsize = chunksize(p);
      nclears = clearsize / sizeof(INTERNAL_SIZE_T);
      assert(nclears >= 2);

      if (nclears > 10) {
        MALLOC_ZERO(d, clearsize);
      }

      else {
        *(d+0) = 0;
        *(d+1) = 0;
	if (nclears > 2) {
	  *(d+2) = 0;
	  *(d+3) = 0;
	  if (nclears > 4) {
	    *(d+4) = 0;
	    *(d+5) = 0;
	    if (nclears > 6) {
	      *(d+6) = 0;
	      *(d+7) = 0;
	      if (nclears > 8) {
		*(d+8) = 0;
		*(d+9) = 0;
	      }
            }
          }
        }
      }
    }
#if ! MMAP_CLEARS
    else
    {
      d = (INTERNAL_SIZE_T*)mem;
      clearsize = chunksize(p);
      MALLOC_ZERO(d, clearsize);
    }
#endif
    /* Set guard again, since we just cleared it
     */
    guard_set(get_malloc_state()->guard_stored, p, (n_elements * elem_size), p->size);
  }

  return mem;
}

/*
  ------------------------------ valloc ------------------------------
*/

#if __STD_C
DL_STATIC Void_t* vALLOc(size_t bytes)
#else
DL_STATIC Void_t* vALLOc(bytes) size_t bytes;
#endif
{
  /* Ensure initialization */
  mstate av = get_malloc_state();
  if (!av || av->max_fast == 0) {
    malloc_consolidate(av);
    av = get_malloc_state();
  }
  return mEMALIGn(av->pagesize, bytes);
}

/*
  ------------------------------ pvalloc ------------------------------
*/


#if __STD_C
DL_STATIC Void_t* pVALLOc(size_t bytes)
#else
DL_STATIC Void_t* pVALLOc(bytes) size_t bytes;
#endif
{
  mstate av = get_malloc_state();
  size_t pagesz;

  /* Ensure initialization */
  if (!av || av->max_fast == 0) {
    malloc_consolidate(av);
    av = get_malloc_state();
  }
  pagesz = av->pagesize;
  return mEMALIGn(pagesz, (bytes + pagesz - 1) & ~(pagesz - 1));
}
   

/*
  ------------------------------ malloc_trim ------------------------------
*/

#if __STD_C
DL_STATIC int mTRIm(size_t pad)
#else
DL_STATIC int mTRIm(pad) size_t pad;
#endif
{
  mstate av = get_malloc_state();
  /* Ensure initialization/consolidation */
  malloc_consolidate(av);
  av = get_malloc_state();
#ifndef MORECORE_CANNOT_TRIM
  if (morecore32bit(av))
    return sYSTRIm(pad, av);
  else
    return 0;
#else
  return 0;
#endif
}



/*
  ------------------------- malloc_usable_size -------------------------
*/

#if __STD_C
DL_STATIC size_t mUSABLe(Void_t* mem)
#else
DL_STATIC size_t mUSABLe(mem) Void_t* mem;
#endif
{
  chunkinfoptr p;
  if (mem != 0) {
    p = hashtable_lookup(mem);
    if (p && inuse(p)) return chunksize(p);
  }
  return 0;
}

/*
  ------------------------------ mallinfo ------------------------------
*/

DL_STATIC struct mallinfo mALLINFo()
{
  mstate av = get_malloc_state();
  struct mallinfo mi;
  unsigned int i;
  mbinptr b;
  chunkinfoptr p;
  INTERNAL_SIZE_T avail;
  INTERNAL_SIZE_T fastavail;
  int nblocks;
  int nfastblocks;

  /* Ensure initialization */
  if (!av || (av->max_fast == 0)) {
    malloc_consolidate(av);
    av = get_malloc_state();
  }
  check_malloc_state();

  /* Account for top */
  avail = chunksize(av->top);
  nblocks = 1;  /* top always exists */

  /* traverse fastbins */
  nfastblocks = 0;
  fastavail = 0;

  for (i = 0; i < NFASTBINS; ++i) {
    for (p = av->fastbins[i]; p != 0; p = p->fd) {
      ++nfastblocks;
      fastavail += chunksize(p);
    }
  }

  avail += fastavail;

  /* traverse regular bins */
  for (i = 1; i < NBINS; ++i) {
    b = bin_at(av, i);
    for (p = last(b); p != b; p = p->bk) {
      ++nblocks;
      avail += chunksize(p);
    }
  }

  mi.smblks = nfastblocks;
  mi.ordblks = nblocks;
  mi.fordblks = avail;
  mi.uordblks = av->sbrked_mem - avail;
  mi.arena = av->sbrked_mem;
  mi.hblks = av->n_mmaps;
  mi.hblkhd = av->mmapped_mem;
  mi.fsmblks = fastavail;
  mi.keepcost = chunksize(av->top);
  mi.usmblks = av->max_total_mem;
  return mi;
}

/*
  ------------------------------ malloc_stats ------------------------------
*/

DL_STATIC void mSTATs()
{
  struct mallinfo mi = mALLINFo();

  mstate av = get_malloc_state();

  fprintf(stderr, "max system bytes = %10lu\n",
          (CHUNK_SIZE_T)(mi.usmblks));
  fprintf(stderr, "system bytes     = %10lu  (%10lu sbrked, %10lu mmaped)\n",
          (CHUNK_SIZE_T)(mi.arena + mi.hblkhd),
          (CHUNK_SIZE_T)(mi.arena),
          (CHUNK_SIZE_T)(mi.hblkhd));
  fprintf(stderr, "in use bytes     = %10lu\n",
          (CHUNK_SIZE_T)(mi.uordblks + mi.hblkhd));
  fprintf(stderr, "\nhashtable:\n");
  dump_metadata(stderr, &(av->htbl), false);

  
}


/*
  ------------------------------ mallopt ------------------------------
*/

#if __STD_C
DL_STATIC int mALLOPt(int param_number, int value)
#else
DL_STATIC int mALLOPt(param_number, value) int param_number; int value;
#endif
{
  mstate av = get_malloc_state();
  /* Ensure initialization/consolidation */
  malloc_consolidate(av);
  av = get_malloc_state();

  switch(param_number) {
  case M_MXFAST:
    if (value >= 0 && value <= MAX_FAST_SIZE) {
      set_max_fast(av, value);
      return 1;
    }
    else
      return 0;

  case M_TRIM_THRESHOLD:
    av->trim_threshold = value;
    return 1;

  case M_TOP_PAD:
    av->top_pad = value;
    return 1;

  case M_MMAP_THRESHOLD:
    av->mmap_threshold = value;
    return 1;

  case M_MMAP_MAX:
    if (value != 0)
      return 0;
    av->n_mmaps_max = value;
    return 1;

  default:
    return 0;
  }
}


/*	$OpenBSD: arc4random.c,v 1.19 2008/06/04 00:50:23 djm Exp $	*/

/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Arc4 random number generator for OpenBSD.
 *
 * This code is derived from section 17.1 of Applied Cryptography,
 * second edition, which describes a stream cipher allegedly
 * compatible with RSA Labs "RC4" cipher (the actual description of
 * which is a trade secret).  The same algorithm is used as a stream
 * cipher called "arcfour" in Tatu Ylonen's ssh package.
 *
 * Here the stream cipher has been modified always to include the time
 * when initializing the state.  That makes it impossible to
 * regenerate the same random sequence twice, so this can't be used
 * for encryption, but will generate good random numbers.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */

/* Moved u_int8_t -> unsigned char (portability)
 * Eliminated unneeded functions, added read from /dev/urandom taken from:
 $MirOS: contrib/code/Snippets/arc4random.c,v 1.3 2008-03-04 22:53:14 tg Exp $
 * Modified by Robert Connolly from OpenBSD lib/libc/crypt/arc4random.c v1.11.
 * This is arc4random(3) using urandom.
 */

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/time.h>

struct arc4_stream {
	unsigned char i;
	unsigned char j;
	unsigned char s[256];
};

static int rs_initialized;
static struct arc4_stream rs;
static pid_t arc4_stir_pid;
static int arc4_count;

static unsigned char arc4_getbyte(void);

static void
arc4_init(void)
{
	int     n;

	for (n = 0; n < 256; n++)
		rs.s[n] = n;
	rs.i = 0;
	rs.j = 0;
}

static inline void
arc4_addrandom(unsigned char *dat, int datlen)
{
	int     n;
	unsigned char si;

	rs.i--;
	for (n = 0; n < 256; n++) {
		rs.i = (rs.i + 1);
		si = rs.s[rs.i];
		rs.j = (rs.j + si + dat[n % datlen]);
		rs.s[rs.i] = rs.s[rs.j];
		rs.s[rs.j] = si;
	}
	rs.j = rs.i;
}

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

static void
arc4_stir(void)
{
	int     i;
        struct {
                struct timeval tv1;
                struct timeval tv2;
                u_int rnd[(128 - 2*sizeof(struct timeval)) / sizeof(u_int)];
        } rdat;
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
        size_t sz = 0;
	int    fd;
#endif
 
        gettimeofday(&rdat.tv1, NULL);


	if (!rs_initialized) {
		arc4_init();
		rs_initialized = 1;
	}

#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)

#ifdef HAVE_SCHED_YIELD
	/* Yield the processor to introduce some random delay. */
	(void) sched_yield();
#endif

	/*
	 * Pthread problem in multithreaded code on *BSD.
	 */
        fd = open("/dev/urandom", O_RDONLY);
        if (fd != -1) {
                sz = (size_t)read(fd, rdat.rnd, sizeof (rdat.rnd));
                close(fd);
        }
        if (sz > sizeof (rdat.rnd))
                sz = 0;
 #endif

	arc4_stir_pid = getpid();
        gettimeofday(&rdat.tv2, NULL);

        arc4_addrandom((void *)&rdat, sizeof(rdat));

	/*
	 * Discard early keystream, as per recommendations in:
	 * http://www.wisdom.weizmann.ac.il/~itsik/RC4/Papers/Rc4_ksa.ps
	 */
	for (i = 0; i < 256; i++)
		(void)arc4_getbyte();
	arc4_count = 1600000;
}

static unsigned char
arc4_getbyte(void)
{
	unsigned char si, sj;

	rs.i = (rs.i + 1);
	si = rs.s[rs.i];
	rs.j = (rs.j + si);
	sj = rs.s[rs.j];
	rs.s[rs.i] = sj;
	rs.s[rs.j] = si;
	return (rs.s[(si + sj) & 0xff]);
}


 /* Changed to return char* */
static char *
dnmalloc_arc4random(void)
{
	static char val[4];
	
	/* We only call this once, hence no need for locking. */

	/* _ARC4_LOCK(); */
	arc4_count -= 4;
	if (arc4_count <= 0 || !rs_initialized || arc4_stir_pid != getpid())
		arc4_stir();

	val[0] = (char) arc4_getbyte();
	val[1] = (char) arc4_getbyte();
	val[2] = (char) arc4_getbyte();
	val[3] = (char) arc4_getbyte();

	arc4_stir();
	/* _ARC4_UNLOCK(); */
	return val;
}

#if !defined(USE_SYSTEM_MALLOC)
#else
int dnmalloc_pthread_init() { return 0; }
#endif /* ! USE_SYSTEM_MALLOC */



 /* DistriNet malloc (dnmalloc): a more secure memory allocator. 
   Copyright (C) 2005, Yves Younan, Wouter Joosen, Frank Piessens and Rainer Wichmann
   The authors can be contacted by:
      Email: dnmalloc@fort-knox.org
      Address:
      	     Yves Younan
      	     Celestijnenlaan 200A
      	     B-3001 Heverlee
      	     Belgium
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
   
*/

/* Current version: dnmalloc 1.0  */
/* Includes arc4random from OpenBSD, which is under the BDS license     */

/* Versions:
   0.1-0.5:
   Proof of concept implementation by Hans Van den Eynden and Yves Younan
   0.6-0.7:
   Bug fixes by Yves Younan
   0.8-1.0.beta4:
   Reimplementation from scratch by Yves Younan
   1.0.beta4:
   Public release
   1.0.beta5:
   Prev_chunkinfo speeded up, was really slow because of the way we did lookups
   A freechunkinfo region is now freed when it is completely empty and 
   not the current one

   1.0 (Rainer Wichmann [support at la dash samhna dot org]):
   ---------------------

   Compiler warnings fixed
   Define REALLOC_ZERO_BYTES_FREES because it's what GNU libc does
       (and what the standard says)
   Removed unused code
   Fix       assert(aligned_OK(chunk(newp)));
         ->  assert(aligned_OK(chunk(oldp)));
   Fix statistics in sYSMALLOc
   Fix overwrite of av->top in sYSMALLOc
   Provide own assert(), glibc assert() doesn't work (calls malloc)
   Fix bug in mEMALIGn(), put remainder in hashtable before calling fREe
   Remove cfree, independent_cmalloc, independent_comalloc (untested
       public functions not covered by any standard)
   Provide posix_memalign (that one is in the standard)
   Move the malloc_state struct to mmapped memory protected by guard pages 
   Add arc4random function to initialize random canary on startup
   Implement random canary at end of (re|m)alloced/memaligned buffer,
       check at free/realloc
   Remove code conditional on !HAVE_MMAP, since mmap is required anyway.
   Use standard HAVE_foo macros (as generated by autoconf) instead of LACKS_foo

   Profiling: Reorder branches in hashtable_add, next_chunkinfo, 
                  prev_chunkinfo, hashtable_insert, mALLOc, fREe, request2size,
	          checked_request2size (gcc predicts if{} branch to be taken).
	      Use UNLIKELY macro (gcc __builtin_expect()) where branch
                  reordering would make the code awkward.

   Portability: Hashtable always covers full 32bit address space to
                avoid assumptions about memory layout.
   Portability: Try hard to enforce mapping of mmapped memory into
                32bit address space, even on 64bit systems.
   Portability: Provide a dnmalloc_pthread_init() function, since
                pthread locking on HP-UX only works if initialized
		after the application has entered main().
   Portability: On *BSD, pthread_mutex_lock is unusable since it
                calls malloc, use spinlocks instead.
   Portability: Dynamically detect whether the heap is within
                32bit address range (e.g. on Linux x86_64, it isn't).
		Don't use sbrk() if the heap is mapped to an address 
		outside the 32bit range, since this doesn't work with 
		the hashtable. New macro morecore32bit.
                
   Success on: HP-UX 11.11/pthread, Linux/pthread (32/64 bit),
               FreeBSD/pthread, and Solaris 10 i386/pthread.
   Fail    on: OpenBSD/pthread (in  _thread_machdep_save_float_state),
               might be related to OpenBSD pthread internals (??).
	       Non-treaded version (#undef USE_MALLOC_LOC) 
	       works on OpenBSD.
   
   There may be some bugs left in this version. please use with caution.
*/



/* Please read the following papers for documentation: 

   Yves Younan, Wouter Joosen, and Frank Piessens, A Methodology for Designing
   Countermeasures against Current and Future Code Injection Attacks,
   Proceedings of the Third IEEE International Information Assurance
   Workshop 2005 (IWIA2005), College Park, Maryland, U.S.A., March 2005,
   IEEE, IEEE Press.
   http://www.fort-knox.org/younany_countermeasures.pdf
   
   Yves Younan, Wouter Joosen and Frank Piessens and Hans Van den
   Eynden. Security of Memory Allocators for C and C++. Technical Report
   CW419, Departement Computerwetenschappen, Katholieke Universiteit
   Leuven, July 2005. http://www.fort-knox.org/CW419.pdf
 
 */

/* Compile:
   gcc -fPIC -rdynamic -c -Wall dnmalloc-portable.c
   "Link": 
   Dynamic:
   gcc -shared -Wl,-soname,libdnmalloc.so.0 -o libdnmalloc.so.0.0 dnmalloc-portable.o -lc
   Static:
   ar -rv libdnmalloc.a dnmalloc-portable.o
   
*/

