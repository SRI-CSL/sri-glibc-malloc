#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <time.h>
#include <linux/limits.h>

#include "replaylib.h"

#include "mhook.h"

#include "lphash.h"

#include "malloc.h"

const bool silent_running = true;
/* warns about realloc deviations */
const bool deviation_warnings = false;
/* prints allocations and frees */
const bool track_allocations = false;

const size_t BUFFERSZ = 1024;

#define BZCAT "bzcat"

typedef unsigned char uchar;

typedef struct replay_stats_s {
  size_t malloc_count;
  clock_t malloc_clock;
  size_t free_count;
  clock_t free_clock;
  size_t calloc_count;
  clock_t calloc_clock;
  size_t realloc_count;
  clock_t realloc_clock;
} replay_stats_t;



static bool readline(FILE* fp, uchar* buffer, size_t buffersz);
  
static bool replayline(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz);

static bool replay_malloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz);

static bool replay_calloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz);

static bool replay_realloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz);

static bool replay_free(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz);

static bool dirtywork(uintptr_t addresses[], size_t len, const uchar* buffer, size_t buffersz);

static inline float stat2float(size_t count, clock_t clock){
  float retval = 0;
  if(count != 0){
    retval = (clock/(float)count);
  }
  return retval;
}

static void dump_stats(FILE* fp,  replay_stats_t* statsp){
  fprintf(fp, "malloc   %.2f  clocks per call\n",  stat2float(statsp->malloc_count, statsp->malloc_clock));
  fprintf(fp, "free   %.2f  clocks per call\n",  stat2float(statsp->free_count, statsp->free_clock));
  fprintf(fp, "calloc   %.2f  clocks per call\n",  stat2float(statsp->calloc_count, statsp->calloc_clock));
  fprintf(fp, "realloc  %.2f  clocks per call\n",  stat2float(statsp->realloc_count, statsp->realloc_clock));
}


int process_file(const char *filename, bool verbose){
  FILE* fp;
  uchar buffer[BUFFERSZ  + 1];
  size_t linecount;
  lphash_t htbl;
  replay_stats_t stats;
  int code;
  bool compressed = false;
  char pathbuf[PATH_MAX+sizeof(BZCAT)+2] = {0}; // "bzcat " - with terminating NULL
  code = 0;
  linecount = 0;
  fp = NULL;
  buffer[BUFFERSZ] = '\0';   /* this should never be touched again  */

  memset(&stats, 0, sizeof(replay_stats_t));
  
  if (!init_lphash(&htbl)) {
    fprintf(stderr, "Could not initialize the linear pool hashtable: %s\n", strerror(errno));
    return 1;
  }

  if(!strncmp(".bz2", filename + strlen(filename) - 4, 4)) {
    compressed = true;
    snprintf(pathbuf, sizeof(pathbuf), "%s %s", BZCAT, filename);
    fp = popen(pathbuf, "r");
  } else {
    fp = fopen(filename, "r");
  }
  if (fp == NULL) {
    fprintf(stderr, "Could not open %s: %s\n", filename, strerror(errno));
    code = 1;
    goto exit;
  }
  
  linecount = 0;

  
  
  while(readline(fp, buffer, BUFFERSZ)) {
    
    if (!replayline(&htbl, &stats, buffer, BUFFERSZ)) {
      fprintf(stderr, "Replaying line %zu failed: %s\n", linecount, buffer);
      code = 1;
      goto exit;
    } else {
      linecount++;
    }
  }


 exit:

  if (fp != NULL) { 
    if(compressed) {
      pclose(fp);
    } else {
      fclose(fp);
    }
  }


  fprintf(stderr, "Replayed %zu lines from  %s\n", linecount, filename);

  if(verbose){
    //fprintf(stderr, "Replay hash:\n");
    //dump_lphash(stderr, &htbl, false);
    dump_stats(stderr, &stats);
  }
  
  delete_lphash(&htbl);
    
    
  return code;
}


static bool readline(FILE* fp, uchar* buffer, size_t buffersz) {
  size_t index;
  int c;

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));
  
  memset(buffer, '\0', buffersz);

  for(index = 0; index < buffersz; index++) {

    c = fgetc(fp);

    if (c == EOF) {
      return false;
    }
    
    if (c == '\n') {
      return true;
    }

    if ((isalnum(c) == 0) && (c != ' ')) {
      return false;
    }

    buffer[index] = (uchar)c;

  }

  return false;
}

static bool replayline(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz) {
  uchar opchar;
  bool retval;

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }
  
  opchar = buffer[0];
  
  switch(opchar) {
  case 'm': {
    retval = replay_malloc(htbl, statsp, buffer, buffersz); 
    break;
  }
  case 'f': {
    retval = replay_free(htbl, statsp, buffer, buffersz); 
    break;
  }
  case 'c': {
    retval = replay_calloc(htbl, statsp, buffer, buffersz); 
    break;
  }
  case 'r': {
    retval = replay_realloc(htbl, statsp, buffer, buffersz); 
    break;
  }
  case 'i':
  case 'e':
    retval = true; 
    break;
  default : retval = false;
  }
  
  return retval;
}


static void *_r_malloc(replay_stats_t* statsp, size_t size){
  void* rptr;
  clock_t start;

  start = clock();

  rptr = malloc(size);
  
  statsp->malloc_clock += clock() - start;
  statsp->malloc_count++;

  if(track_allocations || rptr == NULL){
    fprintf(stderr, "malloc returned %p of requested size %zu\n", rptr, size);
  }

  return rptr; 
}

static void *_r_realloc(replay_stats_t* statsp, void *ptr, size_t size){
  void* rptr;
  clock_t start;
  
  start = clock();

  rptr  = realloc(ptr, size);

  statsp->realloc_clock += clock() - start;
  statsp->realloc_count++;
  
  if(track_allocations || rptr == NULL){
    fprintf(stderr, "realloc returned %p of requested size %zu\n", rptr, size);
  }

  return rptr;
}

static void * _r_calloc(replay_stats_t* statsp, size_t count, size_t size){
  void* rptr;
  clock_t start;

  start = clock();
    
  rptr = calloc(count, size);

  if(track_allocations || rptr == NULL){
    fprintf(stderr, "realloc returned %p of requested size %zu\n", rptr, count * size);
  }
  
  statsp->calloc_clock += clock() - start;
  statsp->calloc_count++;

  return rptr;
}

static void _r_free(replay_stats_t* statsp, void *ptr){
  clock_t start;

  if(track_allocations){
    fprintf(stderr, "freeing %p\n", ptr);
  }

  start = clock();

  free(ptr);
  
  statsp->free_clock += clock() - start;
  statsp->free_count++;
  

}



static bool replay_malloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz) {
  uintptr_t addresses[MALLOCARGS];
  size_t sz;
  void *key;
  void *val;
  bool success;
 
  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }
  
  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));
  
  assert((buffer[MALLOCLEN] == '\0') && (isalnum(buffer[MALLOCLEN - 1]) != 0));
  
  success = dirtywork(addresses, MALLOCARGS, buffer, buffersz);
  
  if (success) {
    
    if (!silent_running) {
      fprintf(stderr, "malloc(%zu) = %zu @ %zu\n", addresses[0], addresses[1], addresses[2]);
    }

    sz = (size_t)addresses[0];
    key =  (void *)addresses[1];

    val = _r_malloc(statsp, sz);
    
    /* could assert that key is not in the htbl */
    if ( ! lphash_insert(htbl, key, val) ) {
      fprintf(stderr, "Could not insert %p => %p into the htbl: %s\n", key, val, strerror(errno));
      return false;
    }
    
    
    return true;
    
  }
  
  return false;

}

static bool replay_calloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz) {
  uintptr_t addresses[CALLOCARGS];
  size_t sz;
  size_t cnt;
  void *key;
  void *val;
  bool success;

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[CALLOCLEN] == '\0') && (isalnum(buffer[CALLOCLEN - 1]) != 0));

  success = dirtywork(addresses, CALLOCARGS, buffer, buffersz);

  if (success) {

    if (!silent_running) {
      fprintf(stderr, "calloc(%zu, %zu) = %zu  @ %zu\n", addresses[0], addresses[1], addresses[2], addresses[3]);
    }

    cnt = (size_t)addresses[0];
    sz = (size_t)addresses[1];
    key =  (void *)addresses[2];

    val = _r_calloc(statsp, cnt, sz);
    
    /* could assert that key is not in the htbl */
    if ( ! lphash_insert(htbl, key, val) ) {
      fprintf(stderr, "Could not insert %p => %p into the htbl: %s\n", key, val, strerror(errno));
      return false;
    }
    
    return true;

  }

  return false;

}

static bool replay_realloc(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz) {
  uintptr_t addresses[REALLOCARGS];
  void *ptr_in;
  void *ptr_out;
  size_t sz;
  void *val_old;
  void *val_new;
  bool success;

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[REALLOCLEN] == '\0') && (isalnum(buffer[REALLOCLEN - 1]) != 0));

  success = dirtywork(addresses, REALLOCARGS, buffer, buffersz);

  if (success) {

    if (!silent_running) {
      fprintf(stderr, "realloc(%zu, %zu) = %zu  @ %zu\n", addresses[0], addresses[1], addresses[2], addresses[3]);
    }

    /* == Danger zone ==
     *
     * if sz is 0 then this is a free! And a minimum sized object is returned. HUH?!?
     *
     * ALSO: we may not get the same behavior as the run we are trying to mimic.
     * not much we can do but roll with the punches. Though we can inform the
     * user that we have deviated.
     *
     */

    
    ptr_in = (void *)addresses[0];
    sz = (size_t)addresses[1];
    ptr_out =  (void *)addresses[2];

    if (ptr_in == NULL) {
      
      val_old = NULL;
      
    } else {
    
      val_old = lphash_lookup(htbl, ptr_in);

      if (val_old == NULL) {
	fprintf(stderr, "replay_realloc: Failed to find %p in the htbl\n", ptr_in);
	/* iam: could wing it here  and just do a malloc */
	return false;
      }

    }

    val_new = _r_realloc(statsp, val_old, sz);

    if (sz == 0) {

      /* this is a free */
      
      lphash_delete(htbl, ptr_in);

      /* could assert that key is not in the htbl */
      if ( ! lphash_insert(htbl, ptr_out, val_new) ) {
	fprintf(stderr, "replay_realloc: Could not insert %p => %p into the htbl: %s\n", ptr_out, val_new, strerror(errno));
	return false;
      }

    } else {

      /* ok it was a real attempt to grow ptr_in */

      if (ptr_in == ptr_out) {

	/* it was successful; was our attempt successful too? */
	
	if (val_old == val_new) {

	  // nothing to do.


	} else {

	  if(deviation_warnings){
	    fprintf(stderr, "replay_realloc: deviating from script %p != %p in our run %p == %p in script\n",
		    val_old, val_new, ptr_in, ptr_out);
	  }

	  lphash_delete(htbl, ptr_in);

	  if ( ! lphash_insert(htbl, ptr_out, val_new) ) {
	    fprintf(stderr, "replay_realloc: Could not insert %p => %p into the htbl: %s\n", ptr_out, val_new, strerror(errno));
	    return false;
	  }
	}
	
	
      } else {
	
	/* it was unsuccessful; was our attempt unsuccessful too? */
	
	if (val_old == val_new) {
	  
	  if(deviation_warnings){
	    fprintf(stderr, "replay_realloc: deviating from script %p == %p in our run %p != %p in script\n",
		    val_old, val_new, ptr_in, ptr_out);
	  }
	  
	}

	/* either way still need to update the htbl */
	
	lphash_delete(htbl, ptr_in);
	
	if ( ! lphash_insert(htbl, ptr_out, val_new) ) {
	  fprintf(stderr, "replay_realloc: Could not insert %p => %p into the htbl: %s\n", ptr_out, val_new, strerror(errno));
	  return false;
	}
	
      }
            
    }
 
    return true;
    
  }
  
  return false;

}

static bool replay_free(lphash_t* htbl, replay_stats_t* statsp, const uchar* buffer, size_t buffersz) {
  uintptr_t addresses[FREEARGS];
  void *key;
  void *val;
  bool success;

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[FREELEN] == '\0') && (isalnum(buffer[FREELEN - 1]) != 0));

  success = dirtywork(addresses, FREEARGS, buffer, buffersz);

  if (success) {

    if (!silent_running) {
      fprintf(stderr, "free(%zu) @ %zu\n", addresses[0], addresses[1]);
    }

    key = (void *)addresses[0];
    
    if (key == NULL) {
      val = NULL;

      _r_free(statsp, key);

    } else {
      val = lphash_lookup(htbl, key);
      if (val != NULL) {

	_r_free(statsp, val);
	
	lphash_delete(htbl, key);
      } else {
	/* this is a pretty common occurence */
	if (!silent_running) {
	  fprintf(stderr, "replay_free: Failed to find %p in the htbl\n", key);
	}
	return true;
      }
    }


    return true;

  }

  return false;

}


static bool dirtywork(uintptr_t addresses[], size_t len, const uchar* buffer, size_t buffersz) {
  uintptr_t address;
  size_t index;
  char* data;
  char* rest;

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ') || (len >= buffersz) || (len < 1)) {
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  data = (char *)&buffer[2];  
  
  for(index = 0; index < len; index++) {

    rest  = NULL;
    address = strtoll(data, &rest, 16);

    if (data == rest) { return false; }
    
    assert((rest != NULL) && ((rest[0] == ' ') || (rest[0] == '\0')));
    
    addresses[index] = address;
    data = &rest[1];
    
  }

  return true;

}
