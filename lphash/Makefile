

CFLAGS = -Wall -DNDEBUG -O3

#-O3 -DNDEBUG -g

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: lphash.o test.o
	${CC} ${CFLAGS} lphash.o test.o -o test

clean:
	rm -rf *~ *.o test test.dSYM




