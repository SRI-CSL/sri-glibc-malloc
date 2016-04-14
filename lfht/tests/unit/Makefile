LFHT_HOME=../..


OS   =  $(shell uname)

LIBRARYNAME=liblfht

ifeq (Darwin, $(findstring Darwin, ${OS}))
LIB = ${LFHT_HOME}/lib/${LIBRARYNAME}.dylib
else
LIB = ${LFHT_HOME}/lib/${LIBRARYNAME}.so
endif

CFLAGS= -Wall  -I${LFHT_HOME}/include -I${LFHT_HOME}/arch/${ARCH}
LDFLAGS=

all: test_one


%: %.c
	${CC} ${CFLAGS} $< -o $@  ${LIB}


ifeq (Darwin, $(findstring Darwin, ${OS}))

check: test_one
	DYLD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_one

else

check:
	LD_LIBRARY_PATH=${LFHT_HOME}/lib/ ./test_one
endif

