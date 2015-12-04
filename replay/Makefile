
CFLAGS = -Wall  -I ../lphash -I ../dsmalloc

LPLIBRARYNAME=liblphash
DSLIBRARYNAME=libdsmalloc

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LPLIB = ${LPLIBRARYNAME}.dylib
DSLIB = 
CFLAGS = -Wall  -I ../lphash -I ../dsmalloc
else
# LINUX
LPLIB = ${LPLIBRARYNAME}.so
DSLIB = ${DSLIBRARYNAME}.so
CFLAGS = -Wall  -I ../lphash -I ../dsmalloc 
#-DUSE_DL_PREFIX -DHAVE_MMAP
endif

all: ${LPLIB} ${DSLIB}
	${CC} ${CFLAGS} replay.c ${LPLIB} ${DSLIB} -o replay -lpthread


${LPLIB}:
	cd ../lphash; make
	cp ../lphash/${LPLIB} .

${DSLIB}:
ifneq (Darwin, $(findstring Darwin, ${OS}))
	cd ../dsmalloc; make
	cp ../dsmalloc/${DSLIB}* .
endif

clean:
	rm -rf *~ replay ${LPLIBRARYNAME}.* ${DSLIBRARYNAME}.*

test:  all
ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
	./replay ../../analysis/data/yices_main.txt
else
# LINUX
	LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_main.txt
endif


#LD_PRELOAD=./dsmalloc/libdsmalloc.so LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_main.txt

