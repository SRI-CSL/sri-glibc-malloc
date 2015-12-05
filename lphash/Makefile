



# CFLAGS = -Wall -fPIC -DNDEBUG -O3
CFLAGS = -Wall -fPIC -g

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
	$(CC) ${CFLAGS} lphash.o ${LIBFLAGS} -o ${LIB} 


test: ${LIB} test.o
	${CC} ${CFLAGS} test.o lphash.o -o test	


clean:
	rm -rf *~ *.o test test.dSYM *.so *.dylib




