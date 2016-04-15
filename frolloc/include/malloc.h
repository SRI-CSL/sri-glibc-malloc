#ifndef __FROLLOC_MALLOC_H__
#define __FROLLOC_MALLOC_H__

#include <stdlib.h>



extern void frolloc_noop(void)   __attribute__((constructor)) ;
extern void frolloc_delete(void) __attribute__((destructor)) ;


extern void* malloc(size_t sz);
extern void free(void* ptr);
extern void *realloc(void *object, size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void *memalign(size_t boundary, size_t size);
extern int posix_memalign(void **memptr, size_t alignment, size_t size);
extern void malloc_stats(void);

#endif	/* __FROLLOC_MALLOC_H__ */

