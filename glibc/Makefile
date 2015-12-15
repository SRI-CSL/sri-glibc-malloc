#ian's kludgey way of developing.

GLIBC_BUILD=../../Variants/glibc-build/

all: update build

update:
	cp malloc/*.[ch]  malloc/Makefile ${GLIBC_BUILD}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD}



