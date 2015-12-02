INSTALLDIR = /usr/local/lib

DEFS = -DHAVE_MMAP=1          \
	-DHAVE_UNISTD_H=1     \
	-DHAVE_SYS_UIO_H=1    \
	-DHAVE_WRITEV=1       \
	-DHAVE_SYS_PARAM_H=1  \
	-DHAVE_FCNTL_H=1      \
	-DHAVE_SYS_MMAN_H=1   \
	-DUSE_MALLOC_LOCK=1   \
	-DPARANOIA=9          

#        -DDNMALLOC_DEBUG=1


OS   =  $(shell uname)
ARCH =  $(shell uname -p)

OPTIMIZATION=0


ifeq (Darwin, $(findstring Darwin, ${OS}))
######  DARWIN

BUILD_PLATFORM=DARWIN

CFLAGS = -O${OPTIMIZATION} -fPIC $(DEFS) -DHAVE_MEMCPY -DDARWIN -Wall
LIB = libdsmalloc.dynlib
LIBFLAGS = -dynamiclib 
LINK = libdsmalloc.dynlib

else

######## LINUX

BUILD_PLATFORM=LINUX

CFLAGS = -g -fPIC $(DEFS) -rdynamic -Wall -O${OPTIMIZATION}
LIB = libdsmalloc.so.1.0
LIBFLAGS = -g -shared -Wl,-soname,libdsmalloc.so.1
LINK = -L. -ldsmalloc


endif

TCFLAGS=  $(CFLAGS) -g -pthread

metaobj= hashfns.o memcxt.o pool.o metadata.o 

mobj= dsassert.o dsmalloc.o

all: $(LIB) tests 


$(LIB): $(metaobj) $(mobj)
	$(CC) -O${OPTIMIZATION} $(LIBFLAGS) -o $(LIB) $(metaobj) $(mobj)
ifeq ($(BUILD_PLATFORM),LINUX)
	rm -f libdsmalloc.so && ln -s libdsmalloc.so.1.0 libdsmalloc.so && \
	rm -f libdsmalloc.so.1 && ln -s libdsmalloc.so.1.0 libdsmalloc.so.1
endif


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


tests: test mtest0 mtest1 mtest2

test: $(metaobj) test.o dsassert.o
	${CC} hashfns.o memcxt.o pool.o metadata.o test.o  dsassert.o -o test

mtest0: mtest0.c $(metaobj) $(mobj)
	$(CC) $(TCFLAGS) -o mtest0 mtest0.c $(metaobj) $(mobj)

mtest1: mtest1.c $(metaobj) $(mobj)
	$(CC) $(TCFLAGS) -o mtest1 mtest1.c $(metaobj) $(mobj)


clean:
	rm -rf *~ *.o test *.dSYM mtest0 mtest1 mtest2 $(LIB)




