

CFLAGS= -Wall -DCK_CLIENT_LIBRARY  -I../lfpa

LDFLAGS = -L../lfpa -lcklfpa -lck

all:
	${CC} ${CFLAGS} test.c -o test ${LDFLAGS} 


run:
	LD_LIBRARY_PATH=../lfpa ./test


clean:
	rm -f test
