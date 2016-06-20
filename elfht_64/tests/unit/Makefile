LFHT_HOME=../..


OS   =  $(shell uname)

LIBRARYNAME=liblfht

ifeq (Darwin, $(findstring Darwin, ${OS}))
LDFLAGS =
else
LDFLAGS = -lpthread
endif

CFLAGS= -Wall  -I${LFHT_HOME}/include -I${LFHT_HOME}/arch/${ARCH} -g

OBJS = ${LFHT_HOME}/obj/*.o



TESTS = test_one test_two test_three

all: ${TESTS}


%: %.c
	${CC} ${CFLAGS} $< -o $@ ${LDFLAGS} ${OBJS}


check: ${TESTS}

	./test_one
	./test_two  32 1
	./test_three 4
	./test_three 8
	./test_two  32 4
	./test_two  32 8
	./test_two  32 16
	./test_two  64 16

clean:
	${RM} -f ${TESTS}
