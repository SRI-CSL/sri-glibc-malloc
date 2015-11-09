

CFLAGS = -Wall -g

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: hashfns.o  memcxt.o pool.o linhash.o test.o
	${CC} hashfns.o memcxt.o pool.o linhash.o test.o -o test

clean:
	rm -rf *~ *.o test test.dSYM




