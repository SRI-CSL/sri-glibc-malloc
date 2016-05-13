LFHT_HOME=../..


OS   =  $(shell uname)

LIBRARYNAME=liblfht

ifeq (Darwin, $(findstring Darwin, ${OS}))
LIB = ${LFHT_HOME}/lib/${LIBRARYNAME}.dylib
LDFLAGS =
else
LIB = ${LFHT_HOME}/lib/${LIBRARYNAME}.so
LDFLAGS = -lpthread
endif

CFLAGS= -Wall  -I${LFHT_HOME}/include -I${LFHT_HOME}/arch/${ARCH} -ggdb

TESTS = test_one test_two test_three test_four

all: ${TESTS}


%: %.c
	${CC} ${CFLAGS} $< -o $@ ${LDFLAGS} ${LIB}


ifeq (Darwin, $(findstring Darwin, ${OS}))

check: ${TESTS}

	DYLD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_one
	DYLD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_two  32
	DYLD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_three
	DYLD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_four 32

else

check: ${TESTS}

	LD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_one
	LD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_two   32
	LD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_three
	LD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_four 32

endif

clean:
	${RM} -f ${TESTS}
