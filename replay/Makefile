
CFLAGS = -Wall  -I ../lphash

LIBRARYNAME=liblphash

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LIB = ${LIBRARYNAME}.dylib
else
# LINUX
LIB = ${LIBRARYNAME}.so
endif

all: ${LIB}
	${CC} ${CFLAGS} replay.c ${LIB} -o replay


${LIB}:
	cd ../lphash; make
	cp ../lphash/${LIB} .


clean:
	rm -f *~ replay ${LIBRARYNAME}.*

test:  all
	./replay ../../analysis/data/yices_main.txt



