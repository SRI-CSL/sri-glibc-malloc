#ifndef _META_H
#define _META_H


extern void meta_init(void);

extern void meta_exit(void);

/*
 * The work horse. Given any pointer, returns the metadata associated
 * with that ptr, NULL otherwise
 * 
 */
extern void* meta_lookup(void* client_ptr);



/*
 * Adds the metadata. The client chunk is a field of the meta_ptr.
 * 
 */
extern void  meta_add(void* meta_ptr);

#endif
