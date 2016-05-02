ARCH ?= $(shell uname -m)


ifneq ($(ARCH), x86_64)
  error  "Got me just one ARCH today."
endif


OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
LDFLAGS =
else
LDFLAGS = -lpthread
endif

CFLAGS= -Wall -std=gnu11 -I../../include/  -I../../arch/${ARCH} -ggdb

OBJDIR=../../obj

OBJS = ${OBJDIR}/lf_fifo_queue.o

EXES = main test0 test1 test2

all: ${EXES}

main: main.c
	${CC} ${CFLAGS} main.c ${OBJS} -o main ${LDFLAGS}

test0: test0.c
	${CC} ${CFLAGS} test0.c ${OBJS} -o test0 ${LDFLAGS}


test1: test1.c
	${CC} ${CFLAGS} test1.c ${OBJS} -o test1 ${LDFLAGS}

test2: test2.c
	${CC} ${CFLAGS} test2.c ${OBJS} -o test2 ${LDFLAGS}




check: all
	./test0
	./test1
	./test2

clean:
	rm -f test0 test1 test2
	rm -rf *.dSYM

