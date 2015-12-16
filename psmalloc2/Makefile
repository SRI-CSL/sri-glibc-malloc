# Makefile for ptmalloc, version 2
# by Wolfram Gloger 1996-1999, 2001, 2002, 2003, 2004, 2006
# iam: trimmed and cleaned up 

SYS_FLAGS  =
OPT_FLAGS  = -g -O # -O2
WARN_FLAGS = -Wall -Wstrict-prototypes

SH_FLAGS   = -shared -fpic -lpthread

INC_FLAGS  = -Isysdeps/generic

# Flags for the test programs
T_FLAGS   = -DUSE_MALLOC=1 -DTEST=1

# Flags for the compilation of malloc.c
M_FLAGS   = -DTHREAD_STATS=1 #-DMALLOC_DEBUG=1

# Thread flags.
# See the platform-specific targets below.
THR_FLAGS = -DUSE_TSD_DATA_HACK -D_REENTRANT
THR_LIBS  = -lpthread

RM        = rm -f
AR        = ar
RANLIB    = ranlib

MALLOC_OBJ = malloc.o malloc-stats.o
LIB_MALLOC = libmalloc.a

T_SUF =
TESTS = t-test1$(T_SUF) t-test2$(T_SUF) \
        tst-mallocstate$(T_SUF) tst-mstats$(T_SUF)

CFLAGS = $(SYS_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) $(THR_FLAGS) $(INC_FLAGS)

IAMS = -D_GNU_SOURCE=1 -lpthread

.c.o:
	$(CC) -c $(CFLAGS) $<

all: malloc.so 
#$(LIB_MALLOC) $(TESTS)

malloc.o: malloc.c malloc.h
	$(CC) -c $(CFLAGS) $(M_FLAGS) $<

malloc-stats.o: malloc-stats.c malloc.h
	$(CC) -c $(CFLAGS) $(M_FLAGS) $<

libmalloc.a: $(MALLOC_OBJ)
	$(AR) cr $@ $(MALLOC_OBJ)
	$(RANLIB) $@

shared: malloc.so

malloc.so: malloc.c malloc-stats.c malloc.h
	$(CC)   $(SH_FLAGS) $(CFLAGS) $(M_FLAGS) ${IAMS} malloc.c malloc-stats.c  -o $@

clean:
	$(RM) $(MALLOC_OBJ) libmalloc.a malloc.so $(TESTS) core core.[0-9]*

t-test1$(T_SUF): t-test1.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test1.c $(LIB_MALLOC) $(THR_LIBS) -o $@

t-test2$(T_SUF): t-test2.c t-test.h $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) t-test2.c $(LIB_MALLOC) $(THR_LIBS) -o $@

tst-mallocstate$(T_SUF): tst-mallocstate.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mallocstate.c $(LIB_MALLOC) \
	 $(THR_LIBS) -o $@

tst-mstats$(T_SUF): tst-mstats.c $(LIB_MALLOC)
	$(CC) $(CFLAGS) $(T_FLAGS) tst-mstats.c $(LIB_MALLOC) \
	 $(THR_LIBS) -o $@



check: $(TESTS)
	./t-test1
	./t-test2
	./tst-mallocstate || echo "Test mallocstate failed!"
	./tst-mstats || echo "Test mstats failed!"


# dependencies
malloc.o: arena.c hooks.c
