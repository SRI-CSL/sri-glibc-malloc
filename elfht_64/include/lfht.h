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


/* Expanding Lock Free Hash Table (using only 64 bit CAS) */

#ifndef __LFHT_H__
#define __LFHT_H__


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

enum tombstone { TOMBSTONE = 0 };

#define RESIZE_RATIO 0.6

/* (1 << 31) or 2^31 */
#define MAX_TABLE_SIZE ((uint32_t)0x80000000u)

#define KEY_ALIGNMENT  0x10

typedef enum lfht_state { INITIAL, EXPANDING, EXPANDED } lfht_state_t;

typedef struct lfht_entry_s {
  volatile uint64_t  key;
  volatile uint64_t  val;
} lfht_entry_t;

typedef struct lfht_hdr_s lfht_hdr_t;

struct lfht_hdr_s {
  /* flag to indicate if this table no longer contains relevant key/value pairs */
  atomic_bool assimilated;
  /* the "sizeof" the mmapped region that is the header + table  */
  uint64_t sz;
  /* length of the table in units of lfht_entry_t's */
  uint32_t max;
  /* threshold beyond which we should grow the table */
  uint32_t threshold;
  /* the number of non-zero keys in the table */
  atomic_uint_least32_t count;
  /* pointer to the immediate predecessor table */
  lfht_hdr_t *next;
  /* the actual table */
  lfht_entry_t *table;
};


typedef struct lfht_s {
  uintptr_t hdr:62, state:2;
} lfht_t;


static inline lfht_hdr_t *lfht_table_hdr(lfht_t* ht){
  return (lfht_hdr_t *)(ht->hdr << 2);
}

static inline lfht_state_t lfht_state(lfht_t* ht){
  return ht->state;
}

static inline void lfht_set(lfht_t* ht, lfht_hdr_t * hdr, lfht_state_t state){
  ht->state = state;
  ht->hdr = (uintptr_t)hdr >> 2;
}

/* 
 *  Initializes an uninitialized lfht_t struct.
 *  Used like so:
 *
 *  lfht_t ht;
 *  bool success = init_lfht(&ht, 4096);
 *  if(success){
 *   //off to races
 *  
 *  }
 * 
 *   - max is the initial number of key value pairs that the table will be able to store. 
 *
 */

extern bool init_lfht(lfht_t *ht, uint32_t max);

extern bool delete_lfht(lfht_t *ht);

/*
 * Insert or update the value of key in the table to val. 
 * 
 * - key must be 16 byte aligned.
 * - val *must* not be TOMBSTONE.
 *
 */
extern bool lfht_add(lfht_t *ht, uint64_t key, uint64_t val);

/*
 * Remove the value of key in the table, i.e. set it to TOMBSTONE. 
 */
extern bool lfht_remove(lfht_t *ht, uint64_t key);


extern bool lfht_find(lfht_t *ht, uint64_t key, uint64_t *valp);
  

extern void lfht_stats(FILE* fp, const char* name, lfht_t *ht);


#endif
