#include "cktable.h"
#include <stdio.h>

bool 
table_init(struct ck_malloc *allocator, ck_ht_t *htp, size_t size){
  //the mode of our ht
  int mode = CK_HT_MODE_DIRECT | CK_HT_WORKLOAD_DELETE;
  
  if (ck_ht_init(htp, mode, NULL, allocator, size, 666) == false) {
    perror("ck_ht_init");
    return false;
  }
  return true;
}

bool
table_insert(ck_ht_t *htp, void * key, void * value)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, (uintptr_t)key);
  ck_ht_entry_set_direct(&entry, h, (uintptr_t)key, (uintptr_t)value);
  return ck_ht_put_spmc(htp, h, &entry);
}


void *
table_get(ck_ht_t *htp, void * key)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;

  ck_ht_hash_direct(&h, htp, (uintptr_t)key);
  ck_ht_entry_key_set_direct(&entry, (uintptr_t)key);
  if (ck_ht_get_spmc(htp, h, &entry) == true)
    return (void *)ck_ht_entry_value_direct(&entry);
  return 0;
}


bool
table_remove(ck_ht_t *htp, void * key)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, (uintptr_t)key);
  ck_ht_entry_key_set_direct(&entry, (uintptr_t)key);
  return ck_ht_remove_spmc(htp, h, &entry);
}


bool
table_replace(ck_ht_t *htp, void * key, void * value)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, (uintptr_t)key);
  ck_ht_entry_set_direct(&entry, h, (uintptr_t)key, (uintptr_t)value);
  return ck_ht_set_spmc(htp, h, &entry);
}

size_t
table_count(ck_ht_t *htp)
{
  return ck_ht_count(htp);
}

bool
table_reset(ck_ht_t *htp)
{
  return ck_ht_reset_spmc(htp);
}


