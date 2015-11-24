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


OS   =  $(shell uname)
ARCH =  $(shell uname -p)


ifeq (Darwin, $(findstring Darwin, ${OS}))
######  DARWIN
BUILD_PLATFORM=DARWIN

CFLAGS = -O2 -fPIC $(DEFS) -DHAVE_MEMCPY -DDARWIN -Wall
LIB = libdsmalloc.dynlib
LIBFLAGS = -dynamiclib 
LINK = libdsmalloc.dynlib

else

######## LINUX
BUILD_PLATFORM=LINUX

CFLAGS = -g -fPIC $(DEFS) -rdynamic -Wall -O2 
LIB = libdsmalloc.so.1.0
LIBFLAGS = -g -shared -Wl,-soname,libdsmalloc.so.1
LINK = -L. -ldsmalloc


endif

# -Wall -g
#-O3 -DNDEBUG 

TCFLAGS= -g -pthread -DUSE_MALLOC=1

all: $(LIB) test 


$(LIB): dsmalloc.o hashfns.o memcxt.o pool.o metadata.o dsassert.o
	$(CC) -O2 $(LIBFLAGS) -o $(LIB) dsmalloc.o hashfns.o memcxt.o pool.o metadata.o dsassert.o
ifeq ($(BUILD_PLATFORM),LINUX)
	rm -f libdsmalloc.so && ln -s libdsmalloc.so.1.0 libdsmalloc.so && \
	rm -f libdsmalloc.so.1 && ln -s libdsmalloc.so.1.0 libdsmalloc.so.1
endif


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: hashfns.o memcxt.o pool.o metadata.o test.o dsassert.o
	${CC} hashfns.o memcxt.o pool.o metadata.o test.o  dsassert.o -o test

mtest: mt-test1
	@./mtestdrive.sh

mt-test1: mt-test1.c $(LIB)
	$(CC) $(TCFLAGS) -o mt-test1 mt-test1.c $(LINK)


clean:
	rm -rf *~ *.o test test.dSYM mt-test1 $(LIB)




