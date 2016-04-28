ARCH ?= $(shell uname -m)


ifneq ($(ARCH), x86_64)
  error  "Got me just one ARCH today."
endif


CFLAGS= -Wall -I../../include/  -I../../arch/${ARCH} -ggdb


OBJDIR=../../obj

OBJS= ${OBJDIR}/lf_fifo_queue.o 


all:
	${CC} ${CFLAGS} test0.c ${OBJS} -o  test0
	${CC} ${CFLAGS} test1.c ${OBJS} -o  test1

check: all
	./test0
	./test1

clean:
	rm -f test0 test1
	rm -rf *.dSYM

