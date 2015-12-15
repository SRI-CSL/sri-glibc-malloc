#ian's kludgey way of developing.

GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/

all: update build

update:
	cp malloc/*.[ch]  malloc/Makefile ${GLIBC_SRC}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD}



