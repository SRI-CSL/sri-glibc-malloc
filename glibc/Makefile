#ian's kludgey way of developing.

GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/

REPLAYCMD=${HOME}/Repositories/HeapMetadata/src/replay/replay ${HOME}/Repositories/HeapMetadata/analysis/data/yices_smt2_2668e3c6.txt

all: update build

update:
	cp malloc/*.[ch]  malloc/Makefile ${GLIBC_SRC}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD}

test:
	cd ${GLIBC_BUILD}; ./testrun.sh ${REPLAYCMD}


