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

CFLAGS= -Wall -I../../include/  -I../../arch/${ARCH} -ggdb

OBJDIR=../../obj

OBJS = ${OBJDIR}/lf_fifo_queue.o
OBJDIR=../../obj
OBJ_GLOBS = $(addsuffix /*.o,${OBJDIR})
OBJS = $(sort $(wildcard ${OBJ_GLOBS}))

EXES = test0 test1 test2

all: ${EXES}

test0: test0.c
	@echo "OBJS = ${OBJS}"
	${CC} ${CFLAGS} test0.c ${OBJS} -o test0 ${LDFLAGS}

test1: test1.c
	@echo "OBJS = ${OBJS}"
	${CC} ${CFLAGS} test1.c ${OBJS} -o test1 ${LDFLAGS}

test2: test2.c
	@echo "OBJS = ${OBJS}"
	${CC} ${CFLAGS} test2.c ${OBJS} -o test2 ${LDFLAGS}




check: all
	./test0
	./test1
	./test2

clean:
	rm -f test0 test1 test2
	rm -rf *.dSYM

