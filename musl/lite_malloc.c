#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>

#define MUSL_ALIGN 16

void *__expand_heap(size_t *);

void *__simple_malloc(size_t n)
{
	static char *cur, *end;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	size_t align=1, pad;
	void *p;

	if (!n) n++;
	while (align<n && align<MUSL_ALIGN)
		align += align;

	pthread_mutex_lock(&mutex);

	pad = -(uintptr_t)cur & align-1;

	if (n <= SIZE_MAX/2 + MUSL_ALIGN) n += pad;

	if (n > end-cur) {
		size_t m = n;
		char *new = __expand_heap(&m);
		if (!new) {
		  pthread_mutex_unlock(&mutex);
		  return 0;
		}
		if (new != end) {
		  cur = new;
		  n -= pad;
		  pad = 0;
		}
		end = new + m;
	}
	
	p = cur + pad;
	cur += n;
	pthread_mutex_unlock(&mutex);
	return p;
}

//weak_alias(__simple_malloc, malloc);
//weak_alias(__simple_malloc, __malloc0);
