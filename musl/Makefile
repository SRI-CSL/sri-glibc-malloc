

CFLAGS = -Wall -fPIC
LDFLAGS =


OS   =  $(shell uname)


ifeq (Darwin, $(findstring Darwin, ${OS}))
PLATFORM=Darwin
LIB=libmuslloc.dylib
LIBFLAGS = -dynamiclib
else
PLATFORM=Linux
LIB=libmuslloc.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
endif


OBJECTS = __brk.o aligned_alloc.o calloc.o expand_heap.o lite_malloc.o malloc.o malloc_usable_size.o memalign.o posix_memalign.o


all: ${LIB}

${LIB}: ${OBJECTS}
	$(CC) ${OBJECTS} ${LIBFLAGS} -o ${LIB} ${CFLAGS} ${LDFLAGS} 



%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

clean:
	rm -f ${OBJECTS} ${LIB}


