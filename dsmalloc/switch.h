#ifndef _SWITCH_H
#define _SWITCH_H


#ifdef USE_DL_PREFIX

#ifndef HAVE_MMAP
#define HAVE_MMAP
#endif

#ifndef _DSMALLOC_H
#include "dsmalloc.h"
#endif

#define malloc(X)      dlmalloc(X)
#define calloc(X, Y)   dlcalloc(X, Y)
#define realloc(X, Y)  dlrealloc(X, Y)
#define free(X)        dlfree(X)
#define malloc_stats() dlmalloc_stats()
#else 
#include <assert.h>
#endif





#endif
