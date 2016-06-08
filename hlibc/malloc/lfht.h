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


/* Lock Free Hash Table -- Non expanding version */

#ifndef __SRI_LFHT_H__
#define __SRI_LFHT_H__


#include <stdint.h>
#include <stdbool.h>


typedef struct lfht_entry_s {
  uintptr_t  key;
  uintptr_t  val;
} lfht_entry_t;


typedef struct lfht_s {
  //length of the table in units of lfht_entry_t's
  uint32_t max;
  //the sizeof the table 
  uint64_t sz;
  lfht_entry_t *table;
} lfht_t;

/* 
  Initializes an uninitialized lfht_t struct.
  Used like so:

  lfht_t ht;
  bool success = init_lfht(&ht, 4096);
  if(success){
   //off to races
  
  }
 
   - max is the maximum number of key value pairs that the table will be able to store. 
     It is a hard limit.

*/
extern bool init_lfht(lfht_t *ht, uint32_t max);

extern bool delete_lfht(lfht_t *ht);

extern bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val);

extern bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp);
  

#endif
