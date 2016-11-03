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

#ifndef _MEMCXT_H
#define _MEMCXT_H


#include <stdint.h>
#include <stdio.h>

#include "chunkinfo.h"

/*
 *  The API of our pool allocator for the metadata.
 *  
 *
 */



typedef struct memcxt_s {
  segment_pool_t* segments;
  bucket_pool_t* buckets;
} memcxt_t;


typedef enum { DIRECTORY, SEGMENT, BUCKET } memtype_t;

extern bool init_memcxt(memcxt_t* memcxt);

/* 
 * Attempts to allocate a block of memory of the appropriate type from the memcxt.
 * The size and oldptr are onlye used when the type is DIRECTORY. 
 * Since in the other two cases the size of the object required is fixed at 
 * compile time.
 * In the case of a directory we use the oldptr to see if we can realloc the
 * old directory to the newly desired size (which could be smaller).
 *
 * All these routines can return NULL.
 *
 */
extern void* memcxt_allocate(memcxt_t* memcxt, memtype_t type, void* oldptr, size_t size);

extern void memcxt_release(memcxt_t* memcxt, memtype_t,  void*, size_t);

extern void delete_memcxt(memcxt_t* memcxt);

extern void dump_memcxt(FILE* fp, memcxt_t* memcxt);

#endif
