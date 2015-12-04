
CFLAGS = -Wall  -I ../dsmalloc

LIBRARYNAME=libdsmalloc

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LIB = ${LIBRARYNAME}.dylib
CFLAGS = -Wall  -I ../dsmalloc
else
# LINUX
LIB = ${LIBRARYNAME}.so
CFLAGS = -Wall  -I ../dsmalloc 
#-DUSE_DL_PREFIX -DHAVE_MMAP
endif

OBJECTS = replay.o lphash.o

all: ${LIB} ${OBJECTS}
	${CC} ${CFLAGS} replay.o lphash.o -o replay


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 



#${LIB}  -lpthread

${LIB}:
ifneq (Darwin, $(findstring Darwin, ${OS}))
	$(MAKE) -C ../dsmalloc
	cp ../dsmalloc/${DSLIB}* .
endif

clean:
	rm -rf *~ replay ${LIBRARYNAME}.*
	$(MAKE) -C ../dsmalloc clean

test:  all
ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt

else
# LINUX
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt
endif


#LD_PRELOAD=./dsmalloc/libdsmalloc.so LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_smt2_2668e3c6.txt

