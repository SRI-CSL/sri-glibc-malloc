DEFS = -DHAVE_MMAP=1          \
	-DHAVE_UNISTD_H=1     \
	-DHAVE_SYS_UIO_H=1    \
	-DHAVE_WRITEV=1       \
	-DHAVE_SYS_PARAM_H=1  \
	-DHAVE_FCNTL_H=1      \
	-DHAVE_SYS_MMAN_H=1   \
        -DUSE_DL_PREFIX	      \
	-DPARANOIA=0

#        -DDNMALLOC_DEBUG=1
#
# iam: using the lock also triggers the metadata_is_consistent checks IFF
#
#        -DMETADATA_CHECKS
#
#	 -DUSE_MALLOC_LOCK=1 
#
# -DMETADATA_CHECKS


CFLAGS = -Wall ${DEFS} -g 

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))

DEFS += -DDARWIN

else


endif


metaobj= hashfns.o memcxt.o pool.o metadata.o 

mobj= dsassert.o dsmalloc.o

robj = replay.o lphash.o

OBJECTS = ${robj} ${metaobj} ${mobj} 

all: ${LIB} ${OBJECTS}
	${CC} ${CFLAGS} ${OBJECTS} -o replay


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 

clean:
	rm -rf *~ replay *.o


test:  all
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt


#LD_PRELOAD=./dsmalloc/libdsmalloc.so LD_LIBRARY_PATH=. ./replay ../../analysis/data/yices_smt2_2668e3c6.txt

