

CFLAGS = -Wall -DNDEBUG -O3


LIBRARYNAME=liblphash

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LIB = ${LIBRARYNAME}.dylib
LIBFLAGS = -dynamiclib
else
# LINUX
LIB = ${LIBRARYNAME}.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
endif



#-O3 -DNDEBUG -g

all: lib test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


lib: lphash.o
	$(CC) lphash.o ${LIBFLAGS} -o ${LIB} 


test: ${LIB} test.o
	${CC} ${CFLAGS} ${LIB} test.o -o test	

clean:
	rm -rf *~ *.o test test.dSYM *.so *.dylib




