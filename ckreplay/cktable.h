#ifndef __CKTABLE_H__
#define __CKTABLE_H__

#include <ck_ht.h>

extern bool 
table_init(struct ck_malloc *allocator, ck_ht_t *htp, size_t size);

extern bool
table_insert(ck_ht_t *htp, void * key, void * value);

extern void *
table_get(ck_ht_t *htp, void * key);

extern bool
table_remove(ck_ht_t *htp, void * key);

extern bool
table_replace(ck_ht_t *htp, void * key, void * value);

extern size_t
table_count(ck_ht_t *htp);

extern bool
table_reset(ck_ht_t *htp);


#endif
