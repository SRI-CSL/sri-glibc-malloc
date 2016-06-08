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

#ifndef __SRI_GLIBC_LOOKUP_H__
#define __SRI_GLIBC_LOOKUP_H__


/*
  Returns the arena the ptr belongs to:

  0: means the main_arena and the ptr is mmapped
  1: means the main_arena and the ptr is sbrked

  N: means the ptr belongs to arena N where N > 1

*/

#include <stddef.h>

void lookup_init(void);

void lookup_destroy(void);

size_t lookup_arena_index(void* ptr);





#endif
