

CFLAGS= -Wall -DUSE_LFPA_PREFIX  -I../lfpa

LDFLAGS = -L../lfpa -llfpa -lck

all:
	${CC} ${CFLAGS} ckallocator.c cktable.c test.c -o test ${LDFLAGS} 


run:
	LD_LIBRARY_PATH=../lfpa ./test


clean:
	rm -f test
