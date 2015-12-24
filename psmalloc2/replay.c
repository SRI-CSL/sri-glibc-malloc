#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>

#include "lphash.h"

#include "malloc.h"

/*
 *  Parses the output from ../mhooks/mhook.c and replays it.
 *
 *  Hopefully in the exact same fashion (but whether our calls to
 *  realloc match the scripts lies in the lap of the malloc gods). 
 *
 *  I guess we will be able to check that (modulo callers) when 
 *  it is finished.
 *
 *  By design it is very unforgiving on the input.
 *
 *  Keep in mind that mhook.c misses stuff at startup, and that the
 *  main idea behind the replay script is to trigger similar bugs
 *  in the client malloc library.
 *
 */

const bool silent_running = true;

const size_t BUFFERSZ = 1024;


typedef unsigned char uchar;

static bool readline(FILE* fp, uchar* buffer, size_t buffersz);
  
static bool replayline(lphash_t* htbl, const uchar* buffer, size_t buffersz);

static bool replay_malloc(lphash_t* htbl, const uchar* buffer, size_t buffersz);

static bool replay_calloc(lphash_t* htbl, const uchar* buffer, size_t buffersz);

static bool replay_realloc(lphash_t* htbl, const uchar* buffer, size_t buffersz);

static bool replay_free(lphash_t* htbl, const uchar* buffer, size_t buffersz);

/* These need to be kept in synch with ../mhooks/mhook.c */

enum mhooklen { MALLOCLEN = 58, FREELEN = 39, CALLOCLEN = 77, REALLOCLEN = 77 };

enum mhookargs { MALLOCARGS = 3, FREEARGS  = 2, CALLOCARGS = 4, REALLOCARGS = 4 };

static bool dirtywork(uintptr_t addresses[], size_t len, const uchar* buffer, size_t buffersz);


int main(int argc, char* argv[]){
  FILE* fp;
  uchar buffer[BUFFERSZ  + 1];
  size_t linecount;
  lphash_t htbl;
  int code;

  code = 0;
  fp = NULL;
  

  if (argc != 2) {
    fprintf(stdout, "Usage: %s <mhook output file>\n", argv[0]);
    return 1;
  }

  buffer[BUFFERSZ] = '\0';   /* this should never be touched again  */
  
  if (!init_lphash(&htbl)) {
    fprintf(stderr, "Could not initialize the linear pool hashtable: %s\n", strerror(errno));
    return 1;
  }

  fp = fopen(argv[1], "r");
  if (fp == NULL) {
    fprintf(stderr, "Could not open %s: %s\n", argv[1], strerror(errno));
    code = 1;
    goto exit;
  }
  
  linecount = 0;

  
  
  while(readline(fp, buffer, BUFFERSZ)) {
    
    if (!replayline(&htbl, buffer, BUFFERSZ)) {
      fprintf(stderr, "Replaying line %zu failed: %s\n", linecount, buffer);
      code = 1;
      goto exit;
    } else {
      linecount++;
    }
  }


  if (!silent_running) {
    fprintf(stdout, "replayed %zu lines from  %s\n", linecount, argv[1]);
    dump_lphash(stdout, &htbl, false);
  }


  
 exit:

  if (fp != NULL) { fclose(fp); }

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

static bool replayline(lphash_t* htbl, const uchar* buffer, size_t buffersz) {
  uchar opchar;
  bool retval;

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  if ((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')) {
    return false;
  }
  
  opchar = buffer[0];

  switch(opchar) {
  case 'm': retval = replay_malloc(htbl, buffer, buffersz); break;
  case 'f': retval = replay_free(htbl, buffer, buffersz); break;
  case 'c': retval = replay_calloc(htbl, buffer, buffersz); break;
  case 'r': retval = replay_realloc(htbl, buffer, buffersz); break;
  default : retval = false;
  }
  
  
  return retval;
}

static bool replay_malloc(lphash_t* htbl, const uchar* buffer, size_t buffersz) {
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
    val = malloc(sz);

    /* could assert that key is not in the htbl */
    if ( ! lphash_insert(htbl, key, val) ) {
      fprintf(stderr, "Could not insert %p => %p into the htbl: %s\n", key, val, strerror(errno));
      return false;
    }
    
    
    return true;
    
  }
  
  return false;

}

static bool replay_calloc(lphash_t* htbl, const uchar* buffer, size_t buffersz) {
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
    val = calloc(cnt, sz);

    /* could assert that key is not in the htbl */
    if ( ! lphash_insert(htbl, key, val) ) {
      fprintf(stderr, "Could not insert %p => %p into the htbl: %s\n", key, val, strerror(errno));
      return false;
    }
    
    return true;

  }

  return false;

}

static bool replay_realloc(lphash_t* htbl, const uchar* buffer, size_t buffersz) {
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

    val_new = realloc(val_old, sz);

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

	  fprintf(stderr, "replay_realloc: deviating from script %p != %p in our run %p == %p in script\n",
		  val_old, val_new, ptr_in, ptr_out);

	  lphash_delete(htbl, ptr_in);

	  if ( ! lphash_insert(htbl, ptr_out, val_new) ) {
	    fprintf(stderr, "replay_realloc: Could not insert %p => %p into the htbl: %s\n", ptr_out, val_new, strerror(errno));
	    return false;
	  }
	}
	
	
      } else {
	
	/* it was unsuccessful; was our attempt unsuccessful too? */
	
	if (val_old == val_new) {
	  
	  fprintf(stderr, "replay_realloc: deviating from script %p == %p in our run %p != %p in script\n",
		  val_old, val_new, ptr_in, ptr_out);
	  
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

static bool replay_free(lphash_t* htbl, const uchar* buffer, size_t buffersz) {
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
      free(key);
    } else {
      val = lphash_lookup(htbl, key);
      if (val != NULL) {
	free(val);
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
