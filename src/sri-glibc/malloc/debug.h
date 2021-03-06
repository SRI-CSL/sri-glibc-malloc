/*
 * Copyright (C) 2016  SRI International
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

#ifndef __SRI_GLIBC_DEBUG_H__
#define __SRI_GLIBC_DEBUG_H__


#include <stdlib.h>


typedef enum lock_action {
  LOCK_ACTION       = 'L',
  UNLOCK_ACTION     = 'U'
} lock_action_t;


typedef enum lock_site {
  MALLOC_SITE           = 0,
  REALLOC_SITE          = 1,
  CALLOC_SITE           = 2,
  FREE_SITE             = 3,
  MEMALIGN_SITE         = 4,
  SYSMALLOC_SITE        = 5,
  TRIM_SITE             = 6,
  MUSABLE_SITE          = 7,
  MALLINFO_SITE         = 8,
  MALLOPT_SITE          = 9,
  MALLOC_STATS_SITE     = 10,
  MALLOC_INFO_SITE      = 11,
  ARENA_SITE            = 12,
} lock_site_t;




#if defined(NDEBUG) || !defined(SRI_MALLOC_LOG)

#define log_init()
#define log_end()

#define log_lock_event(S, A, I, X, T)


#define log_malloc(V, S)
#define log_realloc(V, O, S)
#define log_free(V)

#else 

void log_init(void);
void log_end(void);

void log_lock_event(lock_action_t stage, void* av, int index, lock_site_t site, int tid);


#if 0

void log_malloc(void* val, size_t size);
void log_realloc(void* val, void* oval, size_t size);
void log_free(void* val);

#else

#define log_malloc(V, S)
#define log_realloc(V, O, S)
#define log_free(V)

#endif

#endif

#endif
