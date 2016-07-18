LFHT_HOME=../..


OS   =  $(shell uname)

LIBRARYNAME=liblfht

ifeq (Darwin, $(findstring Darwin, ${OS}))
LDFLAGS =
else
LDFLAGS = -lpthread
endif

CFLAGS= -Wall  -I${LFHT_HOME}/include -I${LFHT_HOME}/arch/${ARCH} -g

OBJDIR=${LFHT_HOME}/obj
OBJ_GLOBS = $(addsuffix /*.o,${OBJDIR})
OBJS = $(sort $(wildcard ${OBJ_GLOBS}))

TESTS = test_one test_three

all: ${TESTS}


%: %.c
	${CC} ${CFLAGS} $< -o $@ ${LDFLAGS} ${OBJS}


check: ${TESTS}
	./test_one
	./test_three 4
	./test_three 8

clean:
	${RM} -f ${TESTS}
