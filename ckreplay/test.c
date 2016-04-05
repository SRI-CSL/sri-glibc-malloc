#include <ck_ht.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>


static size_t max = 1024 * 1024;

size_t allocated = 0;

/*

https://groups.google.com/forum/#!topic/concurrencykit/GezcuxYLPs0

The extra arguments to the free callback are number of bytes of region
being deallocated and bool indicating whether the memory being
destroyed is vulnerable to read-reclaim races (and so, extra
precautions must be taken).

For realloc, the first size_t is current number of bytes of allocation
and the second is the new number of bytes. The bool indicates the same
thing as free, whether safe memory reclamation of some form might be
needed.

*/

static void *
ht_malloc(size_t r)
{
  allocated += r;
  return lfpa_malloc(r);
}

static void
ht_free(void *p, size_t b, bool r)
{
  (void)b;
  (void)r;
  allocated -= b;
  lfpa_free(p);
  return;
}

static void *
ht_realloc(void *p, size_t os, size_t ns, bool r)
{
  (void)os;
  (void)r;
  return lfpa_realloc(p, ns);
}

//our lock free pool allocator
static struct ck_malloc allocator = {
  .malloc = ht_malloc,
  .realloc = ht_realloc,
  .free = ht_free
};



static bool 
table_init(ck_ht_t *htp){
  //the mode of our ht
  int mode = CK_HT_MODE_DIRECT | CK_HT_WORKLOAD_DELETE;
  
  if (ck_ht_init(htp, mode, NULL, &allocator, max, 666) == false) {
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
  size_t i;
  //our ck hash table
  ck_ht_t ht CK_CC_CACHELINE;

  if (table_init(&ht) == false) {
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Creation stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
    bool success = table_insert(&ht, i, i+1);
    if( ! success ){
      fprintf(stderr, "Insertion failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Insertion stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
     uintptr_t val = table_get(&ht, i);
    if( val != i + 1){
      fprintf(stderr, "Retrieval failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Retrieval stage complete. Table size = %zu\n", table_count(&ht));


  for(i = 1; i <= max; i++){
    bool success = table_replace(&ht, i, i+2);
    if( ! success ){
      fprintf(stderr, "Insertion failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Update stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
     uintptr_t val = table_get(&ht, i);
    if( val != i + 2){
      fprintf(stderr, "Retrieval of update failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Update retrieval stage complete. Table size = %zu\n", table_count(&ht));

  for(i = 1; i <= max; i++){
     bool success = table_remove(&ht, i);
    if( ! success ){
      fprintf(stderr, "Removal failed for i = %zu\n", i);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "Removal stage complete. Table size = %zu\n", table_count(&ht));

  table_reset(&ht);

  fprintf(stderr, "OK %zu\n", allocated);
  

  return 0;
}


