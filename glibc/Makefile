#ian's kludgey way of developing.

GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/

REPO=${HOME}/Repositories/HeapMetadata

STEST0=${REPO}/src/tests/stest0
STEST1=${REPO}/src/tests/stest1
STEST2=${REPO}/src/tests/stest2

REPLAYCMD=${REPO}/src/replay/replay ${REPO}/analysis/data/yices_smt2_2668e3c6.txt

all: update build install

update:
	cp malloc/*.[ch]  malloc/Makefile  ${GLIBC_SRC}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD} CFLAGS="-DMALLOC_DEBUG=1 -O1 -g"  

install:
	$(MAKE) -C ${GLIBC_BUILD} install

test:
	$(MAKE) -C ../glibc_tests  run
	cd ${GLIBC_BUILD}; ./testrun.sh ${REPLAYCMD}



#
#
#
