#ifndef _DSASSERT_H
#define _DSASSERT_H


#if defined (__GNUC__) && __GNUC__ > 2
# define LIKELY(expression) (__builtin_expect(!!(expression), 1))
# define UNLIKELY(expression) (__builtin_expect(!!(expression), 0))
# define __attribute_malloc__ __attribute__ ((__malloc__))
#else
# define LIKELY(x)       (x)
# define UNLIKELY(x)     (x)
# define __attribute_malloc__ /* Ignore */
#endif


  /* Using assert() with multithreading will cause the code 
   * to deadlock since glibc __assert_fail will call malloc().
   * We need our very own assert().
   */

typedef void assert_handler_tp(const char * error, const char *file, int line);

#if  PARANOIA > 0

#ifdef NDEBUG
#undef NDEBUG
#endif


#define assert(x)                               \
  do {		                                \
    if (UNLIKELY(!(x))) {			\
      assert_handler(#x, __FILE__, __LINE__);	\
    }                                           \
  } while (0)

#else

#define NDEBUG
#define assert(x) ((void)0)

#endif

extern assert_handler_tp *assert_handler;

extern assert_handler_tp *dnmalloc_set_handler(assert_handler_tp *new);

#endif

