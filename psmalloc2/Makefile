# Makefile for ptmalloc, version 2
# by Wolfram Gloger 1996-1999, 2001, 2002, 2003, 2004, 2006
# iam: snip snip; trimmed and cleaned up 

LDFLAGS = -shared -fpic -lpthread

CFLAGS = -D_GNU_SOURCE=1                          \
	-g -O                                     \
	-Wall -Wstrict-prototypes                 \
	-DUSE_TSD_DATA_HACK -D_REENTRANT          \
	-Isysdeps/pthread -Isysdeps/generic -I. 

# Flags for the test programs
T_FLAGS   = -DUSE_MALLOC=1 -DTEST=1

# Flags for the compilation of malloc.c
M_FLAGS   = -DTHREAD_STATS=1 
#-DMALLOC_DEBUG=1


MALLOC_OBJ = malloc.o malloc-stats.o
LIB_MALLOC = libmalloc.a

TESTS = t-test1 t-test2 tst-mallocstate tst-mstats

.c.o:
	$(CC) -c $(CFLAGS) $<

all: malloc.so $(LIB_MALLOC) $(TESTS)

malloc.o: malloc.c malloc.h
	$(CC) -c $(CFLAGS) $(M_FLAGS) $<

malloc-stats.o: malloc-stats.c malloc.h
	$(CC) -c $(CFLAGS) $(M_FLAGS) $<

libmalloc.a: $(MALLOC_OBJ)
	ar cr $@ $(MALLOC_OBJ)
	ranlib $@

shared: malloc.so

malloc.so: malloc.c malloc-stats.c malloc.h
	$(CC)  $(CFLAGS) $(M_FLAGS)  malloc.c malloc-stats.c $(LDFLAGS) -o $@

clean:
	rm -f $(MALLOC_OBJ) libmalloc.a malloc.so $(TESTS)

t-test1: t-test1.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test1.c $(LIB_MALLOC) -lpthread -o $@

t-test2: t-test2.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test2.c $(LIB_MALLOC) -lpthread -o $@

tst-mallocstate: tst-mallocstate.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mallocstate.c $(LIB_MALLOC) -lpthread -o $@

tst-mstats: tst-mstats.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mstats.c $(LIB_MALLOC) -lpthread  -o $@



check: $(TESTS)
	./t-test1
	./t-test2
	./tst-mallocstate || echo "Test mallocstate failed!"
	./tst-mstats || echo "Test mstats failed!"


# dependencies
malloc.o: arena.c hooks.c
