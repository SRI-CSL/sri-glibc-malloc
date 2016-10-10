#ifndef __GASSERT_H__
#define __GASSERT_H__

#include <features.h>
#include <stddef.h>
#include <stdio.h>
#include "sri.h"
#include "debug.h"
#include "lookup.h"

#ifdef NDEBUG
# define assert(expr) ((void) 0)
#else
# define assert(expr) \
  ((expr)								      \
   ? ((void) 0)								      \
   : __malloc_assert (#expr, __FILE__, __LINE__, __func__))

extern const char *__progname;

static void
__malloc_assert (const char *assertion, const char *file, unsigned int line,
		 const char *function)
{
  log_end();
  (void) __fxprintf (NULL, "%s%s%s:%u: %s%sAssertion `%s' failed.\n",
		     __progname, __progname[0] ? ": " : "",
		     file, line,
		     function ? function : "", function ? ": " : "",
		     assertion);
#if SRI_DUMP_LOOKUP
  lookup_dump(stderr, false);
  fflush (stderr);
#endif
  
  abort ();
}
#endif



#endif