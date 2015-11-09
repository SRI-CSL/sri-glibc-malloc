

CFLAGS = -Wall -g

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: hashfns.o linhash.o test.o memcxt.o
	${CC} hashfns.o linhash.o memcxt.o test.o -o test

clean:
	rm -rf *~ *.o test test.dSYM




