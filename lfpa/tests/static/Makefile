

CFLAGS= -Wall -I../../include/ -ggdb


OBJDIR=../../obj

OBJS= ${OBJDIR}/malloc.o


all:
	${CC} ${CFLAGS} stest0.c ${OBJS} -o  stest0
	${CC} ${CFLAGS} stest1.c ${OBJS} -o  stest1
	${CC} ${CFLAGS} stest2.c ${OBJS} -o  stest2


check: all
	./stest0
	./stest1
	./stest2



clean:
	rm -f stest0 stest1 stest2
	rm -rf *.dSYM

