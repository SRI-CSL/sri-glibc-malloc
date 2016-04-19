#ifndef __FROLLOC_DEBUG_H__
#define __FROLLOC_DEBUG_H__


#include <stdlib.h>

#if defined(NDEBUG) || !defined(SRI_DEBUG)
#define log_init()
#define log_malloc(V, S)
#define log_realloc(V, O, S)
#define log_calloc(V, N, S)
#define log_free(V)
#define log_end()
#else 
void log_init(void);
void log_malloc(void* val, size_t size);
void log_realloc(void* val, void* oval, size_t size);
void log_calloc(void* val, size_t nmemb, size_t size);
void log_free(void* val);
void log_end(void);
#endif





#endif
