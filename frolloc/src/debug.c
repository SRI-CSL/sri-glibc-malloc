#ifndef NDEBUG

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>


/* a quick 'n dirty version of mhook */

#include "debug.h"

static int logfd = -1;

static const char hex[16] = "0123456789ABCDEF";

static const char logfile[] = "/tmp/frolloc.log";


enum mhooklen { MALLOCLEN = 58, FREELEN = 39, CALLOCLEN = 77, REALLOCLEN = 77 };

enum mhookargs { MALLOCARGS = 3, FREEARGS  = 2, CALLOCARGS = 4, REALLOCARGS = 4 };


// A set of logging functions that don't call malloc() and friends....
// Code assumes LP64 model, and sizeof(size_t) <= sizeof(uintptr_t)

static void storehexstring(char *buf, uintptr_t val)
{
  int pos = 2 * sizeof(val) - 1;
  int t;
  while(val > 0 && pos >= 0) {
    t = val & 0xF;
    buf[pos--] = hex[t];
    val >>= 4;
  }
}

void log_init (void)
{
  int fd ;
  fd = open(logfile, O_WRONLY | O_EXCL | O_CREAT | O_TRUNC, 0600);
  if (fd > 0) {
    logfd = fd;
  }
}


static void _writelogentry(char func, size_t size1, size_t size2, void *p, void *q, void *caller)
{
  char buffer[] = { ' ', ' ', '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', 
		         ' ', '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		         ' ', '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		         ' ', '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		         ' ', '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
		    '\n' };
  int sz = sizeof(buffer) - 1;
  int rcode;
  buffer[0] = func;
  switch (func) {
  case 'm':
    storehexstring(&buffer[4], (uintptr_t)size1);
    storehexstring(&buffer[23], (uintptr_t)p);
    storehexstring(&buffer[42], (uintptr_t)caller);
    sz = MALLOCLEN;
    break;
  case 'f':
    storehexstring(&buffer[4], (uintptr_t)p);
    storehexstring(&buffer[23], (uintptr_t)caller);
    sz = FREELEN;
    break;
  case 'c':
    storehexstring(&buffer[4], (uintptr_t)size1);
    storehexstring(&buffer[23], (uintptr_t)size2);
    storehexstring(&buffer[42], (uintptr_t)p);
    storehexstring(&buffer[61], (uintptr_t)caller);
    sz = CALLOCLEN;
    break;
  case 'r':
    storehexstring(&buffer[4], (uintptr_t)p);
    storehexstring(&buffer[23], (uintptr_t)size1);
    storehexstring(&buffer[42], (uintptr_t)q);
    storehexstring(&buffer[61], (uintptr_t)caller);
    sz = REALLOCLEN;
    break;
  default:
    sz = 5;
  }
  buffer[sz] = '\n';
  
  if(logfd < 0){
    return;
  }
  rcode = write(logfd, buffer, sz+1);
  if(rcode != sz+1) {
    if(rcode < 0){
      if(errno == EINTR){
	exit(3);
      } else if(errno == EBADF){
	exit(5);
      } else {
	exit(errno);
      }
    } else  {
      exit(7);
    }
  }
}


void log_malloc(void* val, size_t size)
{
  _writelogentry('m', size, 0, val, NULL, (void*)pthread_self());
}
void log_realloc(void* val, void* oval, size_t size)
{
  _writelogentry('r', size, 0, oval, val, (void*)pthread_self());
}
void log_calloc(void* val, size_t nmemb, size_t size)
{
  _writelogentry('c', nmemb, size, val, NULL, (void*)pthread_self());
}
void log_free(void* val)
{
  _writelogentry('f', 0, 0, val, NULL, (void*)pthread_self());
}


#endif
