#ifndef _MALLOC_H
#define _MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

void *malloc (size_t);
void *calloc (size_t, size_t);
void *realloc (void *, size_t);
void free (void *);
void *valloc (size_t);
void *memalign(size_t, size_t);

size_t malloc_usable_size(void *);

void malloc_stats(void);

#ifdef __cplusplus
}
#endif

#endif
