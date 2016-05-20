/*
 * Copyright (C) 2007  Scott Schneider, Christos Antonopoulos
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __MAGED_H__
#define __MAGED_H__

#include <stdlib.h>

#ifndef USE_LFPA_PREFIX

extern void* malloc(size_t sz);
extern void free(void* ptr);
extern void *realloc(void *object, size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void *memalign(size_t boundary, size_t size);
extern int posix_memalign(void **memptr, size_t alignment, size_t size);
extern void malloc_stats(void);

#else 

extern void* lfpa_malloc(size_t sz);
extern void  lfpa_free(void* ptr);
extern void *lfpa_realloc(void *object, size_t size);
extern void *lfpa_calloc(size_t nmemb, size_t size);
extern void *lfpa_memalign(size_t boundary, size_t size);
extern int   lfpa_posix_memalign(void **memptr, size_t alignment, size_t size);
extern void  lfpa_malloc_stats(void);


#endif


#endif	/* __MAGED_H__ */

