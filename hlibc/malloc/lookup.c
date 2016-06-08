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

#include <stdint.h>

#include "lookup.h"

/* 
 * The lo and hi addresses of sbrk mem. Note that
 * only the main_arena futzes with these, and when it
 * does it has the main_arena lock. So we do not need
 * to do anything ourselves.
 */

static uintptr_t sbrk_lo;

static uintptr_t sbrk_hi;

void lookup_init(void){

}

void lookup_destroy(void){

}


size_t lookup_arena_index(void* ptr){

  if( sbrk_lo <= (uintptr_t)ptr && (uintptr_t)ptr <= sbrk_hi ){ 
    return 1;
  }




  return 0;
}

