#ian's kludgey way of developing.

GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/

REPO=${HOME}/Repositories/HeapMetadata

STEST0=${REPO}/src/tests/stest0
STEST1=${REPO}/src/tests/stest1
STEST2=${REPO}/src/tests/stest2

REPLAYCMD=${REPO}/src/replay/replay ${REPO}/analysis/data/yices_smt2_2668e3c6.txt

all: update build

update:
	cp malloc/*.[ch]  malloc/Makefile ${GLIBC_SRC}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD}

test:
	cd ${GLIBC_BUILD}; ${STEST0}
	cd ${GLIBC_BUILD}; ${STEST1}
	cd ${GLIBC_BUILD}; ${STEST2}
	cd ${GLIBC_BUILD}; ./testrun.sh ${REPLAYCMD}


