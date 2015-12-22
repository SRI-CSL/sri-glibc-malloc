# Makefile for ptmalloc, version 2
# by Wolfram Gloger 1996-1999, 2001, 2002, 2003, 2004, 2006
# iam: snip snip; trimmed and cleaned up 

LDFLAGS = -shared -fPIC -lpthread

CFLAGS = -D_GNU_SOURCE=1                          \
	-g -O                                     \
	-Wall -Wstrict-prototypes                 \
	-DUSE_TSD_DATA_HACK -D_REENTRANT          \
	-fPIC                                     \
	-Isysdeps/pthread -Isysdeps/generic -I. 

# Flags for the test programs
T_FLAGS   = -DUSE_MALLOC=1 -DTEST=1

# Flags for the compilation of malloc.c
M_FLAGS   = -DTHREAD_STATS=1 
#-DMALLOC_DEBUG=1

METAOBJ= utils.o memcxt.o metadata.o 
MALLOCOBJ = $(METAOBJ) malloc.o malloc-stats.o

LIB_MALLOC = libmalloc.a

TESTS = t-test1 t-test2 tst-mallocstate tst-mstats stest0

%.o: %.c %.h 
	$(CC) $(CFLAGS) $(M_FLAGS) $< -c 

all: $(METAOBJ) malloc.so $(LIB_MALLOC) $(TESTS)

libmalloc.a: $(MALLOCOBJ)
	ar cr $@ $(MALLOCOBJ)
	ranlib $@

shared: $(METAOBJ) malloc.so 

malloc.so: $(MALLOCOBJ) malloc.h
	$(CC)  $(CFLAGS) $(M_FLAGS) $(MALLOCOBJ) $(LDFLAGS) -o $@

clean:
	rm -f $(MALLOCOBJ) libmalloc.a malloc.so $(TESTS)

t-test1: t-test1.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test1.c $(LIB_MALLOC) -lpthread -o $@

t-test2: t-test2.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test2.c $(LIB_MALLOC) -lpthread -o $@

tst-mallocstate: tst-mallocstate.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mallocstate.c $(LIB_MALLOC) -lpthread -o $@

tst-mstats: tst-mstats.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mstats.c $(LIB_MALLOC) -lpthread  -o $@


stest0: $(MALLOCOBJ)
	$(CC) $(CFLAGS) $(T_FLAGS) stest0.c $(MALLOCOBJ)  -lpthread -o  $@


check: $(TESTS)
	./t-test1
	./t-test2
	./tst-mallocstate || echo "Test mallocstate failed!"
	./tst-mstats || echo "Test mstats failed!"


# dependencies
malloc.o: arena.c hooks.c
