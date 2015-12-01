#include "dsassert.h"

#include <unistd.h>
#include <sys/uio.h>
#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include <execinfo.h>

/* Obtain a backtrace and print it to stderr. */
void
print_trace (void)
{
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);
  fprintf (stderr, "Obtained %zd stack frames.\n", size);
  for (i = 0; i < size; i++)
    fprintf (stderr, "%s\n", strings[i]);
  //free (strings);
} 



static void default_assert_handler(const char *error, 
				   const char *file, int line)
{
#ifdef HAVE_WRITEV
  struct iovec iov[5];
  ssize_t rcode;
  char * i1 = "assertion failed (";
  char * i3 = "): ";
  char * i5 = "\n";

  iov[0].iov_base = i1;               iov[0].iov_len = strlen(i1); 
  iov[1].iov_base = (char*) file;     iov[1].iov_len = strlen(file); 
  iov[2].iov_base = i3;               iov[2].iov_len = strlen(i3); 
  iov[3].iov_base = (char*) error;    iov[3].iov_len = strlen(error); 
  iov[4].iov_base = i5;               iov[4].iov_len = strlen(i5); 
  rcode = writev(STDERR_FILENO, iov, 5);
  if(rcode < 0){
    //not much we can do ...
  }
#else
  fputs("assertion failed (", stderr);
  fputs(file, stderr);
  fputs("): ", stderr);
  fputs(error, stderr);
  fputc('\n', stderr);
#endif
  abort();
}

assert_handler_tp *assert_handler = default_assert_handler;


assert_handler_tp *dnmalloc_set_handler(assert_handler_tp *new)
{
  assert_handler_tp *old = assert_handler;
  assert_handler = new;
  return old;
}

