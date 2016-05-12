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

#include <stdlib.h>
#include <sys/mman.h>

#include "lfht_128.h"
#include "util.h"
#include "atomic.h"


bool init_lfht(lfht_t *ht, uint32_t max){
  uint64_t sz;
  void *addr;
  if(ht != NULL && max != 0){
    sz = max * sizeof(lfht_entry_t);
    addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (addr != MAP_FAILED) {
      ht->table = (lfht_entry_t *)addr;
      ht->max = max;
      ht->sz = sz;
      return true;
    }
  }
  return false;
}

bool delete_lfht(lfht_t *ht){
  int retcode;
  
  if(ht != NULL && ht->table != NULL){
    retcode = munmap(ht->table, ht->sz);
    ht->table = NULL;
    if(retcode == 0){
      return true;
    }
  }
  return false;
}

bool lfht_insert(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;
    
  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;

    j = jenkins_hash_ptr((void *)key) & mask; 
    
    i = j;

    while (true) {

      entry = ht->table[i];

      if(entry.key == 0){
	if(cas_128((volatile u128_t *)&(ht->table[i]), *((u128_t *)&entry), *((u128_t *)&desired))){
	  return true;
	} else {
	  continue;
	}
      }

      i++;
      i &= mask;

      if( i == j ){ break; }

    }
    
  }
  return false;
}

bool lfht_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;
  
  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = ht->table[i];

      if(entry.key == key){

	if(cas_128((volatile u128_t *)&ht->table[i], *((u128_t *)&entry), *((u128_t *)&desired))){
	  return true;
	} else {
	  continue;
	}
      } else if(entry.key == 0){
	return false;
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }

    }

  }
	
  return false;

}

bool lfht_insert_or_update(lfht_t *ht, uintptr_t key, uintptr_t val){
  uint32_t mask, j, i;
  lfht_entry_t entry, desired;

  if(ht != NULL  && key != 0){
    desired.key = key;
    desired.val = val;
    mask = ht->max - 1;
    j = jenkins_hash_ptr((void *)key) & mask;
    i = j;

    while (true) {

      entry = ht->table[i];

      if(entry.key == key || entry.key == 0){
	if(cas_128((volatile u128_t *)&ht->table[i], *((u128_t *)&entry), *((u128_t *)&desired))){
	  return true;
	} else {
	  continue;
	}
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }

    }
  }
  return false;
}

bool lfht_find(lfht_t *ht, uintptr_t key, uintptr_t *valp){
  uint32_t mask, j, i;
  uint64_t kval;
    
  mask = ht->max - 1;
  j = jenkins_hash_ptr((void *)key) & mask;
  i = j;
  
  if(ht != NULL && key != 0 && valp != NULL){

    while (true) {

      kval = read_64((volatile uint64_t *)&ht->table[i].key);

      if(kval == 0){
	return false;
      }

      if(kval == key){
	/* tobias does not do an atomic read here */
	*valp = ht->table[i].val;
	return true;
      }

      i++;
      i &= mask;
      
      if( i == j ){ break; }
      
    }
  }

  return false;
}
  
