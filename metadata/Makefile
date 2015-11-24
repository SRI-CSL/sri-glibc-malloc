

CFLAGS = -Wall 

#-O3 -DNDEBUG 

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: hashfns.o memcxt.o pool.o metadata.o test.o
	${CC} hashfns.o memcxt.o pool.o metadata.o test.o -o test

clean:
	rm -rf *~ *.o test test.dSYM




