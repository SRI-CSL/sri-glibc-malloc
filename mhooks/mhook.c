/*
 * Taken from http://stackoverflow.com/questions/17803456/an-alternative-for-the-deprecated-malloc-hook-functionality-of-glibc
 * with TLS from http://elinux.org/images/b/b5/Elc2013_Kobayashi.pdf
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

extern void *__libc_malloc(size_t size);
extern void __libc_free(void *ptr);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void *__libc_realloc(void *ptr, size_t size);

static __thread int malloc_hook_active = 1;

static int logfd = -1;
static bool ourfd = false;

static const char hex[16] = "0123456789ABCDEF";

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
    sz = 58;
    break;
  case 'f':
    storehexstring(&buffer[4], (uintptr_t)p);
    storehexstring(&buffer[23], (uintptr_t)caller);
    sz = 39;
    break;
  case 'c':
    storehexstring(&buffer[4], (uintptr_t)size1);
    storehexstring(&buffer[23], (uintptr_t)size2);
    storehexstring(&buffer[42], (uintptr_t)p);
    storehexstring(&buffer[61], (uintptr_t)caller);
    sz = 77;
    break;
  case 'r':
    storehexstring(&buffer[4], (uintptr_t)p);
    storehexstring(&buffer[23], (uintptr_t)size1);
    storehexstring(&buffer[42], (uintptr_t)q);
    storehexstring(&buffer[61], (uintptr_t)caller);
    sz = 77;
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


static void*
my_calloc_hook (size_t nemb, size_t size, void *caller)
{
  void *result;

  // deactivate hooks for logging
  malloc_hook_active = 0;

  result = calloc(nemb, size);

  // do logging
  _writelogentry('c', nemb, size, result, NULL, caller);

  // reactivate hooks
  malloc_hook_active = 1;

  return result;
}

static void* my_realloc_hook (void *p, size_t size, void *caller)
{
  void *result;

  // deactivate hooks for logging
  malloc_hook_active = 0;

  result = realloc(p, size);

  // do logging
  _writelogentry('r', size, 0, p, result, caller);

  // reactivate hooks
  malloc_hook_active = 1;

  return result;
}

static void* my_malloc_hook (size_t size, void *caller)
{
  void *result;

  // deactivate hooks for logging
  malloc_hook_active = 0;

  result = malloc(size);

  // do logging
  _writelogentry('m', size, 0, result, NULL, caller);

  // reactivate hooks
  malloc_hook_active = 1;

  return result;
}

static void my_free_hook(void *p, void *caller)
{
  // deactivate hooks for logging
  malloc_hook_active = 0;

  free(p);

  // do logging
  _writelogentry('f', 0, 0, p, NULL, caller);

  // reactivate hooks
  malloc_hook_active = 1;

}

void* malloc (size_t size)
{
  void *caller = __builtin_return_address(0);
  if (malloc_hook_active)
    return my_malloc_hook(size, caller);
  else
    return __libc_malloc(size);
}

void free(void *ptr)
{
  void *caller = __builtin_return_address(0);
  if (malloc_hook_active)
    return my_free_hook(ptr, caller);
  else
    return __libc_free(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
  void *caller = __builtin_return_address(0);
  if (malloc_hook_active)
    return my_calloc_hook(nmemb, size, caller);
  else
    return __libc_calloc(nmemb, size);

}

void *realloc(void *ptr, size_t size)
{
    void *caller = __builtin_return_address(0);
  if (malloc_hook_active)
    return my_realloc_hook(ptr, size, caller);
  else
    return __libc_realloc(ptr, size);

}

/* Instead of __attribute ((constructor)), setup to run in init.
   This way we run earlier, and can capture more that happens during startup.
   Unfortunately, we can't run in preinit as part of a DSO.
 */

static void mhook_init (void)
{

  int fd;
  char *envname = secure_getenv("MHOOK");
  if (envname != NULL) {
    fd = open(envname, O_WRONLY | O_EXCL | O_CREAT | O_APPEND, 0600);
    if (fd > 0) {
      logfd = fd;
      ourfd = true;
    }
  }
}

__attribute__((section(".init_array"))) typeof(mhook_init) *__init = mhook_init;

/* It's really tempting to clean up after ourselves, but a bad idea.
   Other destructors may run after us, and malloc/free things, so
   we want to keep the log file open until the exiting program
   finally returns to the kernel, which will clean up after us.
   As long as we're unbuffered, this is the right thing to do.
   If we add buffering, we'll want to flush here, as it's the
   last chance we'll get. The attribute line 
   runs later than if we use __attribute ((destructor)) 
   as part of the function definition.
*/

static void mhook_fini (void)
{
#if 0
  if(ourfd) {
    close(logfd);
    logfd = 2;
  }
#endif
}

__attribute__((section(".fini_array"))) typeof(mhook_fini) *__fini = mhook_fini;


