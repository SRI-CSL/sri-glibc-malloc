/*
* Copyright (c) 1996-1999, 2001-2004 Wolfram Gloger

Permission to use, copy, modify, distribute, and sell this software
and its documentation for any purpose is hereby granted without fee,
provided that (i) the above copyright notices and this permission
notice appear in all copies of the software and related documentation,
and (ii) the name of Wolfram Gloger may not be used in any advertising
or publicity relating to the software.

THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

IN NO EVENT SHALL WOLFRAM GLOGER BE LIABLE FOR ANY SPECIAL,
INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY
OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * $Id: t-test1.c,v 1.2 2004/11/04 14:58:45 wg Exp $
 * by Wolfram Gloger 1996-1999, 2001, 2004
 * A multi-thread test for malloc performance, maintaining one pool of
 * allocated bins per thread.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#if (defined __STDC__ && __STDC__) || defined __cplusplus
# include <stdlib.h>
#endif
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/mman.h>

/*
#if !USE_MALLOC
#include <malloc.h>
#else
#include "malloc.h"
#endif
*/

#ifdef USE_SYSTEM_MALLOC
#define memalign(a,b)  malloc(b)
#else
extern void *memalign(size_t boundary, size_t size);
#endif

static int verbose = 1;

/* dummy for the samhain safe_fatal logger 
 */
void safe_fatal(const char * details, 
				const char * file, int line)
{
  (void) file;
  (void) line;
  fputs("assert failed: ", stderr);
  puts(details);
  _exit(EXIT_FAILURE);
}

/* lran2.h
 * by Wolfram Gloger 1996.
 *
 * A small, portable pseudo-random number generator.
 */

#ifndef _LRAN2_H
#define _LRAN2_H

#define LRAN2_MAX 714025l /* constants for portable */
#define IA	  1366l	  /* random number generator */
#define IC	  150889l /* (see e.g. `Numerical Recipes') */

struct lran2_st {
    long x, y, v[97];
};

static void
lran2_init(struct lran2_st* d, long seed)
{
    long x;
    int j;

    x = (IC - seed) % LRAN2_MAX;
    if(x < 0) x = -x;
    for(j=0; j<97; j++) {
	x = (IA*x + IC) % LRAN2_MAX;
	d->v[j] = x;
    }
    d->x = (IA*x + IC) % LRAN2_MAX;
    d->y = d->x;
}

#ifdef __GNUC__
__inline__
#endif
static long
lran2(struct lran2_st* d)
{
    int j = (d->y % 97);

    d->y = d->v[j];
    d->x = (IA*d->x + IC) % LRAN2_MAX;
    d->v[j] = d->x;
    return d->y;
}

#undef IA
#undef IC

#endif

/*
 * $Id: t-test.h,v 1.1 2004/11/04 14:32:21 wg Exp $
 * by Wolfram Gloger 1996.
 * Common data structures and functions for testing malloc performance.
 */

/* Testing level */
#ifndef TEST
#define TEST 99
#endif

/* For large allocation sizes, the time required by copying in
   realloc() can dwarf all other execution times.  Avoid this with a
   size threshold. */
#ifndef REALLOC_MAX
#define REALLOC_MAX	2000
#endif

struct bin {
	unsigned char *ptr;
	unsigned long size;
};

#if TEST > 0

static void
mem_init(unsigned char *ptr, unsigned long size)
{
	unsigned long i, j;

	if(size == 0) return;
#if TEST > 3
	memset(ptr, '\0', size);
#endif
	for(i=0; i<size; i+=2047) {
		j = (unsigned long)ptr ^ i;
		ptr[i] = ((j ^ (j>>8)) & 0xFF);
	}
	j = (unsigned long)ptr ^ (size-1);
	ptr[size-1] = ((j ^ (j>>8)) & 0xFF);
}

static int
mem_check(unsigned char *ptr, unsigned long size)
{
	unsigned long i, j;

	if(size == 0) return 0;
	for(i=0; i<size; i+=2047) {
		j = (unsigned long)ptr ^ i;
		if(ptr[i] != ((j ^ (j>>8)) & 0xFF)) return 1;
	}
	j = (unsigned long)ptr ^ (size-1);
	if(ptr[size-1] != ((j ^ (j>>8)) & 0xFF)) return 2;
	return 0;
}

static int
zero_check(unsigned* ptr, unsigned long size)
{
	unsigned char* ptr2;

	while(size >= sizeof(*ptr)) {
		if(*ptr++ != 0)
			return -1;
		size -= sizeof(*ptr);
	}
	ptr2 = (unsigned char*)ptr;
	while(size > 0) {
		if(*ptr2++ != 0)
			return -1;
		--size;
	}
	return 0;
}

#endif /* TEST > 0 */

/* Allocate a bin with malloc(), realloc() or memalign().  r must be a
   random number >= 1024. */

static void
bin_alloc(struct bin *m, unsigned long size, int r)
{
#if TEST > 0
	if(mem_check(m->ptr, m->size)) {
	  fprintf(stderr, "memory corrupt!\n");
	  exit(1);
	}
#endif
	r %= 1024;
	/*printf("%d ", r);*/
	if(r < 4) { /* memalign */
		if(m->size > 0) free(m->ptr);
		m->ptr = (unsigned char *)memalign(sizeof(int) << r, size);
		/* fprintf(stderr, "FIXME memalign %p\n", m->ptr); */
	} else if(r < 20) { /* calloc */
		if(m->size > 0) free(m->ptr);
		m->ptr = (unsigned char *)calloc(size, 1);
#if TEST > 0
		if(zero_check((unsigned*)m->ptr, size)) {
			unsigned long i;
			for(i=0; i<size; i++)
				if(m->ptr[i] != 0)
					break;
			fprintf(stderr, "calloc'ed memory non-zero (ptr=%p, i=%ld)!\n", m->ptr, i);
			exit(1);
		}
#endif
		/* fprintf(stderr, "FIXME calloc %p\n", m->ptr); */
	} else if(r < 100 && m->size < REALLOC_MAX) { /* realloc */
		if(m->size == 0) m->ptr = NULL;
		m->ptr = realloc(m->ptr, size);
		/* fprintf(stderr, "FIXME realloc %p\n", m->ptr); */
	} else { /* plain malloc */
		if(m->size > 0) free(m->ptr);
		m->ptr = (unsigned char *)malloc(size);
		/* fprintf(stderr, "FIXME malloc %p\n", m->ptr); */
	}
	if(!m->ptr) {
	  fprintf(stderr, "out of memory (r=%d, size=%ld)!\n", r, (long)size);
	  exit(1);
	}
	m->size = size;
#if TEST > 0
	mem_init(m->ptr, m->size);
#endif
}

/* Free a bin. */

static void
bin_free(struct bin *m)
{
	if(m->size == 0) return;
#if TEST > 0
	if(mem_check(m->ptr, m->size)) {
	  fprintf(stderr, "memory corrupt!\n");
	  exit(1);
	}
#endif
	free(m->ptr);
	m->size = 0;
}

/*
 * Local variables:
 * tab-width: 4
 * End:
 */


struct user_data {
	int bins, max;
	unsigned long size;
	long seed;
};

/*
 * $Id: thread-st.h$
 * pthread version
 * by Wolfram Gloger 2004
 */

#include <pthread.h>
#include <stdio.h>

pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t finish_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef USE_PTHREADS_STACKS
#define USE_PTHREADS_STACKS 0
#endif

#ifndef STACKSIZE
#define STACKSIZE	32768
#endif

struct thread_st {
	char *sp;							/* stack pointer, can be 0 */
	void (*func)(struct thread_st* st);	/* must be set by user */
	pthread_t id;
	int flags;
	struct user_data u;
};

static void
thread_init(void)
{
#if !defined(USE_SYSTEM_MALLOC) && defined(USE_MALLOC_LOCK)
  extern int dnmalloc_pthread_init(void);
  dnmalloc_pthread_init();
#endif

  if (verbose)
	printf("Using posix threads.\n");
  pthread_cond_init(&finish_cond, NULL);
  pthread_mutex_init(&finish_mutex, NULL);
}

static void *
thread_wrapper(void *ptr)
{
	struct thread_st *st = (struct thread_st*)ptr;

	/*printf("begin %p\n", st->sp);*/
	st->func(st);
	pthread_mutex_lock(&finish_mutex);
	st->flags = 1;
	pthread_mutex_unlock(&finish_mutex);
	pthread_cond_signal(&finish_cond);
	/*printf("end %p\n", st->sp);*/
	return NULL;
}

/* Create a thread. */
static int
thread_create(struct thread_st *st)
{
	st->flags = 0;
	{
		pthread_attr_t* attr_p = 0;
#if USE_PTHREADS_STACKS
		pthread_attr_t attr;

		pthread_attr_init (&attr);
		if(!st->sp)
			st->sp = malloc(STACKSIZE+16);
		if(!st->sp)
			return -1;
		if(pthread_attr_setstacksize(&attr, STACKSIZE))
			fprintf(stderr, "error setting stacksize");
		else
			pthread_attr_setstackaddr(&attr, st->sp + STACKSIZE);
		/*printf("create %p\n", st->sp);*/
		attr_p = &attr;
#endif
		return pthread_create(&st->id, attr_p, thread_wrapper, st);
	}
	return 0;
}

/* Wait for one of several subthreads to finish. */
static void
wait_for_thread(struct thread_st st[], int n_thr,
				int (*end_thr)(struct thread_st*))
{
	int i;

	pthread_mutex_lock(&finish_mutex);
	for(;;) {
		int term = 0;
		for(i=0; i<n_thr; i++)
			if(st[i].flags) {
				/*printf("joining %p\n", st[i].sp);*/
				if(pthread_join(st[i].id, NULL) == 0) {
					st[i].flags = 0;
					if(end_thr)
						end_thr(&st[i]);
				} else
					fprintf(stderr, "can't join\n");
				++term;
			}
		if(term > 0)
			break;
		pthread_cond_wait(&finish_cond, &finish_mutex);
	}
	pthread_mutex_unlock(&finish_mutex);
}

/*
 * Local variables:
 * tab-width: 4
 * End:
 */


#define N_TOTAL		10
#ifndef N_THREADS
#define N_THREADS	2
#endif
#ifndef N_TOTAL_PRINT
#define N_TOTAL_PRINT 50
#endif
#ifndef MEMORY
#define MEMORY		8000000l
#endif
#define SIZE		10000
#define I_MAX		10000
#define ACTIONS_MAX	30
#ifndef TEST_FORK
#define TEST_FORK 0
#endif

#define RANDOM(d,s)	(lran2(d) % (s))

struct bin_info {
	struct bin *m;
	unsigned long size, bins;
};

#if TEST > 0

void
bin_test(struct bin_info *p)
{
	unsigned int b;

	for(b=0; b<p->bins; b++) {
		if(mem_check(p->m[b].ptr, p->m[b].size)) {
		  fprintf(stderr, "memory corrupt!\n");
		  abort();
		}
	}
}

#endif

void
malloc_test(struct thread_st *st)
{
    unsigned int b;
	int i, j, actions, pid = 1;
	struct bin_info p;
	struct lran2_st ld; /* data for random number generator */

	lran2_init(&ld, st->u.seed);
#if TEST_FORK>0
	if(RANDOM(&ld, TEST_FORK) == 0) {
		int status;

#if !USE_THR
		pid = fork();
#else
		pid = fork1();
#endif
		if(pid > 0) {
		    /*printf("forked, waiting for %d...\n", pid);*/
			waitpid(pid, &status, 0);
			printf("done with %d...\n", pid);
			if(!WIFEXITED(status)) {
				printf("child term with signal %d\n", WTERMSIG(status));
				exit(1);
			}
			return;
		}
		exit(0);
	}
#endif
	p.m = (struct bin *)malloc(st->u.bins*sizeof(*p.m));
	p.bins = st->u.bins;
	p.size = st->u.size;
	for(b=0; b<p.bins; b++) {
		p.m[b].size = 0;
		p.m[b].ptr = NULL;
		if(RANDOM(&ld, 2) == 0)
			bin_alloc(&p.m[b], RANDOM(&ld, p.size) + 1, lran2(&ld));
	}
	for(i=0; i<=st->u.max;) {
#if TEST > 1
		bin_test(&p);
#endif
		actions = RANDOM(&ld, ACTIONS_MAX);
#if USE_MALLOC && MALLOC_DEBUG
		if(actions < 2) { mallinfo(); }
#endif
		for(j=0; j<actions; j++) {
			b = RANDOM(&ld, p.bins);
			bin_free(&p.m[b]);
		}
		i += actions;
		actions = RANDOM(&ld, ACTIONS_MAX);
		for(j=0; j<actions; j++) {
			b = RANDOM(&ld, p.bins);
			bin_alloc(&p.m[b], RANDOM(&ld, p.size) + 1, lran2(&ld));
#if TEST > 2
			bin_test(&p);
#endif
		}
#if 0 /* Test illegal free()s while setting MALLOC_CHECK_ */
		for(j=0; j<8; j++) {
			b = RANDOM(&ld, p.bins);
			if(p.m[b].ptr) {
			  int offset = (RANDOM(&ld, 11) - 5)*8;
			  char *rogue = (char*)(p.m[b].ptr) + offset;
			  /*printf("p=%p rogue=%p\n", p.m[b].ptr, rogue);*/
			  free(rogue);
			}
		}
#endif
		i += actions;
	}
	for(b=0; b<p.bins; b++)
		bin_free(&p.m[b]);
	free(p.m);
	if(pid == 0)
		exit(0);
}

int n_total=0, n_total_max=N_TOTAL, n_running;

int
my_end_thread(struct thread_st *st)
{
	/* Thread st has finished.  Start a new one. */
#if 0
	printf("Thread %lx terminated.\n", (long)st->id);
#endif
	if(n_total >= n_total_max) {
		n_running--;
	} else if(st->u.seed++, thread_create(st)) {
		printf("Creating thread #%d failed.\n", n_total);
		exit(1);
	} else {
		n_total++;
		if (verbose)
		  if(n_total%N_TOTAL_PRINT == 0)
			printf("n_total = %d\n", n_total);
		
	}
	return 0;
}

#if 0
/* Protect address space for allocation of n threads by LinuxThreads.  */
static void
protect_stack(int n)
{
	char buf[2048*1024];
	char* guard;
	size_t guard_size = 2*2048*1024UL*(n+2);

	buf[0] = '\0';
	guard = (char*)(((unsigned long)buf - 4096)& ~4095UL) - guard_size;
	printf("Setting up stack guard at %p\n", guard);
	if(mmap(guard, guard_size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,
			-1, 0)
	   != guard)
		printf("failed!\n");
}
#endif

int
main(int argc, char *argv[])
{
	int i, bins;
	int n_thr=N_THREADS;
	int i_max=I_MAX;
	unsigned long size=SIZE;
	struct thread_st *st;

#if USE_MALLOC && USE_STARTER==2
	ptmalloc_init();
	printf("ptmalloc_init\n");
#endif

	if(argc > 1) n_total_max = atoi(argv[1]);
	if(n_total_max < 1) n_thr = 1;
	if(argc > 2) n_thr = atoi(argv[2]);
	if(n_thr < 1) n_thr = 1;
	if(n_thr > 100) n_thr = 100;
	if(argc > 3) i_max = atoi(argv[3]);

	if(argc > 4) size = atol(argv[4]);
	if(size < 2) size = 2;

	bins = MEMORY/(size*n_thr);
	if(argc > 5) bins = atoi(argv[5]);
	if(bins < 4) bins = 4;

	/*protect_stack(n_thr);*/

	thread_init();
	printf("total=%d threads=%d i_max=%d size=%ld bins=%d\n",
		   n_total_max, n_thr, i_max, size, bins);

	st = (struct thread_st *)malloc(n_thr*sizeof(*st));
	if(!st) exit(-1);

#if !defined NO_THREADS && (defined __sun__ || defined sun)
	/* I know of no other way to achieve proper concurrency with Solaris. */
	thr_setconcurrency(n_thr);
#endif

	/* Start all n_thr threads. */
	for(i=0; i<n_thr; i++) {
		st[i].u.bins = bins;
		st[i].u.max = i_max;
		st[i].u.size = size;
		st[i].u.seed = ((long)i_max*size + i) ^ bins;
		st[i].sp = 0;
		st[i].func = malloc_test;
		if(thread_create(&st[i])) {
		  fprintf(stderr, "Creating thread #%d failed.\n", i);
		  n_thr = i;
		  exit(1);
		}
		if (verbose)
		  printf("Created thread %lx.\n", (long)st[i].id);
	}

	/* Start an extra thread so we don't run out of stacks. */
	if(0) {
		struct thread_st lst;
		lst.u.bins = 10; lst.u.max = 20; lst.u.size = 8000; lst.u.seed = 8999;
		lst.sp = 0;
		lst.func = malloc_test;
		if(thread_create(&lst)) {
		  fprintf(stderr, "Creating thread #%d failed.\n", i);
		  exit(1);
		} else {
			wait_for_thread(&lst, 1, NULL);
		}
	}

	for(n_running=n_total=n_thr; n_running>0;) {
		wait_for_thread(st, n_thr, my_end_thread);
	}
	for(i=0; i<n_thr; i++) {
		free(st[i].sp);
	}
	free(st);
#if USE_MALLOC
	malloc_stats();
#endif
	if (verbose)
	  printf("Done.\n");
	return 0;
}

/*
 * Local variables:
 * tab-width: 4
 * End:
 */
