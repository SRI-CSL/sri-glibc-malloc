#include <ck_ht.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>


//our lock free pool allocator
static struct ck_malloc allocator;

static bool 
table_init(ck_ht_t *htp){
  //the mode of our ht
  int mode = CK_HT_MODE_DIRECT | CK_HT_WORKLOAD_DELETE;
  
  if (ck_ht_init(htp, mode, NULL, &allocator, 1024, 666) == false) {
    perror("ck_ht_init");
    return false;
  }
  return true;
}

static bool
table_insert(ck_ht_t *htp, uintptr_t key, uintptr_t value)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, key);
  ck_ht_entry_set_direct(&entry, h, key, value);
  return ck_ht_put_spmc(htp, h, &entry);
}


static uintptr_t
table_get(ck_ht_t *htp, uintptr_t key)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;

  ck_ht_hash_direct(&h, htp, key);
  ck_ht_entry_key_set_direct(&entry, key);
  if (ck_ht_get_spmc(htp, h, &entry) == true)
    return ck_ht_entry_value_direct(&entry);
  return 0;
}


static bool
table_remove(ck_ht_t *htp, uintptr_t key)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, key);
  ck_ht_entry_key_set_direct(&entry, key);
  return ck_ht_remove_spmc(htp, h, &entry);
}


static bool
table_replace(ck_ht_t *htp, uintptr_t key, uintptr_t value)
{
  ck_ht_entry_t entry;
  ck_ht_hash_t h;
  
  ck_ht_hash_direct(&h, htp, key);
  ck_ht_entry_set_direct(&entry, h, key, value);
  return ck_ht_set_spmc(htp, h, &entry);
}

static size_t
table_count(ck_ht_t *htp)
{
  return ck_ht_count(htp);
}

static bool
table_reset(ck_ht_t *htp)
{
  return ck_ht_reset_spmc(htp);
}


int main(int argc, char *argv[]){
  size_t max = 1024;
  size_t i;
  //our ck hash table
  ck_ht_t ht CK_CC_CACHELINE;

  lpfa_init(&allocator);


  if (table_init(&ht) == false) {
    exit(EXIT_FAILURE);
  }


  for(i = 1; i <= max; i++){
    bool success = table_insert(&ht, i, i+1);
    if( ! success ){
      fprintf(stderr, "Insertion failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Insertion stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i < max; i++){
     uintptr_t val = table_get(&ht, i);
    if( val != i + 1){
      fprintf(stderr, "Retrieval failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }


  fprintf(stderr, "OK\n");

  return 0;
}


