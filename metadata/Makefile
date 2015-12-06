

CFLAGS = -Wall -g -DNDEBUG 

#-O3 -DNDEBUG 

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

mobj=utils.o memcxt.o metadata.o test.o

test: ${mobj}
	${CC} ${mobj} -o test

clean:
	rm -rf *~ *.o test test.dSYM




