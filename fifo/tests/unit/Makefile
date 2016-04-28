

CFLAGS= -Wall -I../../include/ -ggdb


OBJDIR=../../obj

OBJS= ${OBJDIR}/lf_fifo_queue.o 


all:
	${CC} ${CFLAGS} test0.c ${OBJS} -o  test0

check: all
	./test0

clean:
	rm -f test0
	rm -rf *.dSYM

