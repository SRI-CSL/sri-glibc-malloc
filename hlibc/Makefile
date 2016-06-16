#ian's kludgey way of developing.

ifeq ($(shell whoami),vagrant)
GLIBC_SRC=../../Variants/glibc/
#building uses hard links (can't be on the shared drive)
GLIBC_BUILD=/home/vagrant/glibc-build/
else
GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/
endif


all: update build install

update:
	cp malloc/*.[ch]  malloc/Makefile  ${GLIBC_SRC}/malloc

init:
	cp accoutrements/Makeconfig ${GLIBC_SRC}



#the SRI_DEBUG being 1 flag triggers MALLOC_DEBUG and O0 in the malloc/Makefile
build:
	$(MAKE) -C ${GLIBC_BUILD}

install:
	$(MAKE) -C ${GLIBC_BUILD} install

test:
	$(MAKE) -C ../glibc_tests  run



