
CFLAGS = -Wall  -I ../dsmalloc

DSLIBRARYNAME=libdsmalloc

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
DSLIB = 
CFLAGS = -Wall  -I ../dsmalloc
else
# LINUX
DSLIB = ${DSLIBRARYNAME}.so
CFLAGS = -Wall  -I ../dsmalloc 
#-DUSE_DL_PREFIX -DHAVE_MMAP
endif


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


all: ${DSLIB}
	${CC} ${CFLAGS} replay.o lphash.o -o replay

#${DSLIB}  -lpthread

${DSLIB}:
ifneq (Darwin, $(findstring Darwin, ${OS}))
	cd ../dsmalloc; make
	cp ../dsmalloc/${DSLIB}* .
endif

clean:
	rm -rf *~ replay ${DSLIBRARYNAME}.*

test:  all
ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
	./replay ../../analysis/data/yices_main.txt
else
# LINUX
	./replay ../../analysis/data/yices_main.txt
endif


#LD_PRELOAD=./dsmalloc/libdsmalloc.so LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_main.txt

