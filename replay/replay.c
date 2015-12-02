#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>

/*
 *  Parses the output from ../../analysis/mhook.c and replays it.
 *  Hopefully in the exact same fashion. 
 *  I guess we will be able to check that (modulo callers) when 
 *  it is finished.
 *
 *  By design it is very unforgiving on the input.
 *
 */

const bool silent_running = false;

const size_t BUFFERSZ = 1024;


typedef unsigned char uchar;

static bool readline(FILE* fp, uchar* buffer, size_t buffersz);
  
static bool replayline(const uchar* buffer, size_t buffersz);

static bool replay_malloc(const uchar* buffer, size_t buffersz);

static bool replay_calloc(const uchar* buffer, size_t buffersz);

static bool replay_realloc(const uchar* buffer, size_t buffersz);

static bool replay_free(const uchar* buffer, size_t buffersz);

/* These need to be kept in synch with ../../analysis/mhook.c */
enum mhooklen { MALLOCLEN = 58, FREELEN = 39, CALLOCLEN = 77, REALLOCLEN = 77 };
enum mhookargs { MALLOCARGS = 3, FREEARGS  = 2, CALLOCARGS = 4, REALLOCARGS = 4 };

static bool dirtywork(uintptr_t addresses[], size_t len, const uchar* buffer, size_t buffersz);

int main(int argc, char* argv[]){
  FILE* fp;
  uchar buffer[BUFFERSZ  + 1];
  size_t linecount;

  if(argc != 2){
    fprintf(stdout, "Usage: %s <mhook output file>\n", argv[0]);
    return 1;
  }

  buffer[BUFFERSZ] = '\0';   /* this should never be touched again  */
  

  fp = fopen(argv[1], "r");
  if(fp == NULL){
    fprintf(stderr, "Could not open %s: %s\n", argv[1], strerror(errno));
    return 1;
  }

  linecount = 0;
  
  while(readline(fp, buffer, BUFFERSZ)){
    
    if(!replayline(buffer, BUFFERSZ)){
      fprintf(stderr, "Replaying line %zu failed: %s\n", linecount, buffer);
      return 1;
    } else {
      linecount++;
    }
  }

  fclose(fp);

  if(!silent_running){
    fprintf(stdout, "replayed %zu lines from  %s\n", linecount, argv[1]);
  }

  return 0;
}



static bool readline(FILE* fp, uchar* buffer, size_t buffersz){
  size_t index;
  int c;

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));
  
  memset(buffer, '\0', buffersz);

  for(index = 0; index < buffersz; index++){

    c = fgetc(fp);

    if(c == EOF){
      return false;
    }
    
    if(c == '\n'){
      return true;
    }

    if((isalnum(c) == 0) && (c != ' ')){
      return false;
    }

    buffer[index] = (uchar)c;

  }

  return false;
}

static bool replayline(const uchar* buffer, size_t buffersz){
  uchar opchar;
  bool retval;

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')){
    return false;
  }
  
  opchar = buffer[0];

  switch(opchar){
  case 'm': retval = replay_malloc(buffer, buffersz); break;
  case 'f': retval = replay_free(buffer, buffersz); break;
  case 'c': retval = replay_calloc(buffer, buffersz); break;
  case 'r': retval = replay_realloc(buffer, buffersz); break;
  default : retval = false;
  }
  
  
  return retval;
}

static bool replay_malloc(const uchar* buffer, size_t buffersz){
  uintptr_t addresses[MALLOCARGS];
  bool success;
    
  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')){
    return false;
  }
  
  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));
  
  assert((buffer[MALLOCLEN] == '\0') && (isalnum(buffer[MALLOCLEN - 1]) != 0));
  
  success = dirtywork(addresses, MALLOCARGS, buffer, buffersz);
  
  if(success){
    
    if(!silent_running){
      fprintf(stderr, "malloc(%zu) = %zu @ %zu\n", addresses[0], addresses[1], addresses[2]);
    }
    return true;
    
  }
  
  return false;

}

static bool replay_calloc(const uchar* buffer, size_t buffersz){
  uintptr_t addresses[CALLOCARGS];
  bool success;

  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')){
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[CALLOCLEN] == '\0') && (isalnum(buffer[CALLOCLEN - 1]) != 0));

  success = dirtywork(addresses, CALLOCARGS, buffer, buffersz);

  if(success){

    if(!silent_running){
      fprintf(stderr, "calloc(%zu, %zu) = %zu  @ %zu\n", addresses[0], addresses[1], addresses[2], addresses[3]);
    }
    
    return true;

  }

  return false;

}

static bool replay_realloc(const uchar* buffer, size_t buffersz){
  uintptr_t addresses[REALLOCARGS];
  bool success;

  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')){
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[REALLOCLEN] == '\0') && (isalnum(buffer[REALLOCLEN - 1]) != 0));

  success = dirtywork(addresses, REALLOCARGS, buffer, buffersz);

  if(success){

    if(!silent_running){
      fprintf(stderr, "realloc(%zu, %zu) = %zu  @ %zu\n", addresses[0], addresses[1], addresses[2], addresses[3]);
    }
    
    return true;
    
  }
  
  return false;

}

static bool replay_free(const uchar* buffer, size_t buffersz){
  uintptr_t addresses[FREEARGS];
  bool success;

  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ')){
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  assert((buffer[FREELEN] == '\0') && (isalnum(buffer[FREELEN - 1]) != 0));

  success = dirtywork(addresses, FREEARGS, buffer, buffersz);

  if(success){

    if(!silent_running){
      fprintf(stderr, "free(%zu) @ %zu\n", addresses[0], addresses[1]);
    }
  
    return true;

  }

  return false;

}


static bool dirtywork(uintptr_t addresses[], size_t len, const uchar* buffer, size_t buffersz){
  uintptr_t address;
  size_t index;
  char* data;
  char* rest;

  if((buffer == NULL) || (buffersz != BUFFERSZ) || (buffer[1] != ' ') || (len >= buffersz) || (len < 1)){
    return false;
  }

  assert((buffersz == BUFFERSZ) && (buffer[buffersz] == '\0'));

  data = (char *)&buffer[2];  
  
  for(index = 0; index < len; index++){

    rest  = NULL;
    address = strtoll(data, &rest, 16);

    if(data == rest){ return false; }
    
    assert((rest != NULL) && ((rest[0] == ' ') || (rest[0] == '\0')));
    
    addresses[index] = address;
    data = &rest[1];
    
  }

  return true;

}
