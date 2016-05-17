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

/* 
 *  Lifecyle of a Descriptor:
 *
 *  C : created (allocated) in MallocFromSB
 *
 *  R : retired/recycled; placed in the global linked list of descriptors  (several places)
 *
 *  Q : placed in the Partial queue in it's current sizeclass
 *
 *  A : installed as the active SB of the heap
 *
 *  P : installed as the partial SB of the heap
 *
 *  W : released into the wild
 *
 *
 * So we record:  Stage  Descp  Heap Site  
 *
 * we could record size too, if that becomes needed.
 *
 */

typedef enum desc_stage {
  DESC_CREATED      = 'C',   /* created in MallocFromNewSB desc is either new or popped from the global queue  */
  DESC_RETIRED      = 'R',   /* it is no longer associated with a SB and placed in the  global queue           */
  DESC_QUEUED       = 'Q',   /* it manages a partial SB, and has been put in the sizeclass queue for that size */
  DESC_POPPED       = 'O',   /* it manages a partial SB, and has been popped off the sizeclass queue           */
  DESC_ACTIVATED    = 'A',   /* it becomes the active SB for the heap                                          */
  DESC_DEACTIVATED  = 'D',   /* it is removed as the active SB for the heap (it is about to be FULL)           */
  DESC_PARTIAL      = 'P',   /* it becomes the partial SB for the heap                                         */
  DESC_UNPARTIAL    = 'U',   /* it is removed as the partial SB for the heap                                   */
  DESC_WILD         = 'W'    /* it is released into the wild                                                   */
} desc_stage_t;


typedef enum desc_site {
  MALLOCFROMACTIVE      = 0,
  MALLOCFROMPARTIAL     = 1,
  MALLOCFROMNEWSB       = 2,
  UPDATEACTIVE          = 3,
  HEAPPUTPARTIAL        = 4,
  HEAPGETPARTIAL        = 5,
  REMOVEEMPTYDESC       = 6
} desc_site_t;




#if defined(NDEBUG) || !defined(SRI_MALLOC_LOG)

#define log_init()
#define log_end()

#define log_desc_event(S, D, H, X)


#define log_malloc(V, S)
#define log_realloc(V, O, S)
#define log_free(V)

#else 

void log_init(void);
void log_end(void);

void log_desc_event(desc_stage_t stage, void* desc, void *heap, desc_site_t site);


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
