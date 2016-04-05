#ifndef __CKALLOCATOR_H__
#define __CKALLOCATOR_H__


#include <ck_malloc.h>


bool ck_allocator_init(struct ck_malloc* allocator);

size_t ck_allocated(void);


#endif
