
CFLAGS = -Wall  -I ../lphash -I ../dsmalloc -DUSE_DL_PREFIX -DHAVE_MMAP

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
	${CC} ${CFLAGS} replay.c ${LIB} libdsmalloc.so -o replay -lpthread


${LIB}:
	cd ../lphash; make
	cp ../lphash/${LIB} .


clean:
	rm -f *~ replay ${LIBRARYNAME}.*

test:  all
ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
	./replay ../../analysis/data/yices_main.txt
else
# LINUX
	LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_main.txt
endif


#LD_PRELOAD=./dsmalloc/libdsmalloc.so LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_main.txt

