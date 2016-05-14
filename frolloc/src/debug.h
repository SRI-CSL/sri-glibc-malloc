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

#ifndef __FROLLOC_DEBUG_H__
#define __FROLLOC_DEBUG_H__


#include <stdlib.h>

#if defined(NDEBUG) || !defined(SRI_MALLOC_LOG)
#define log_init()
#define log_malloc(V, S)
#define log_realloc(V, O, S)
#define log_free(V)
#define log_end()
#else 
void log_init(void);
void log_malloc(void* val, size_t size);
void log_realloc(void* val, void* oval, size_t size);
void log_free(void* val);
void log_end(void);
#endif





#endif
