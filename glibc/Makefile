#ian's kludgey way of developing.

ifeq ($(shell whoami),vagrant)
GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=/home/vagrant/glibc-build/
else
GLIBC_SRC=../../Variants/glibc/
GLIBC_BUILD=../../Variants/glibc-build/
endif


all: update build install

update:
	cp malloc/*.[ch]  malloc/Makefile  ${GLIBC_SRC}/malloc

build:
	$(MAKE) -C ${GLIBC_BUILD} CFLAGS="-DMALLOC_DEBUG=1 -g"  

install:
	$(MAKE) -C ${GLIBC_BUILD} install

test:
	$(MAKE) -C ../glibc_tests  run
#
# should port a version of replay into glibc_tests
#cd ${GLIBC_BUILD}; ./testrun.sh ${REPLAYCMD}



