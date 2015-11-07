

CFLAGS = -Wall -g

# meta.o  dynahash.o

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: hashfns.o linhash.o test.o memcxt.o
	${CC} hashfns.o linhash.o memcxt.o test.o -o test

clean:
	rm -f *~ *.o test



