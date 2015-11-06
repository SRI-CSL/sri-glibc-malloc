

CFLAGS = -Wall -g

# meta.o  dynahash.o

all: test 

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

test: linhash.o test.o memcxt.o
	${CC} linhash.o memcxt.o test.o -o test

clean:
	rm -f *~ *.o test



