

CFLAGS= -Wall -I../../include/ -ggdb


OBJDIR=../../obj

OBJ_GLOBS = $(addsuffix /*.o,${OBJDIR})
OBJS = $(sort $(wildcard ${OBJ_GLOBS}))


all:
	${CC} ${CFLAGS} stest0.c ${OBJS} -o  stest0
	${CC} ${CFLAGS} stest1.c ${OBJS} -o  stest1
	${CC} ${CFLAGS} stest2.c ${OBJS} -o  stest2
	${CC} ${CFLAGS} main.c ${OBJS} -o  main -pthread
	${CC} ${CFLAGS} loop.c ${OBJS} -o  loop -pthread


check: all
	./stest0
	./stest1
	./stest2
	./main

clean:
	rm -f stest0 stest1 stest2 main
	rm -rf *.dSYM

