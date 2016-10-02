#ian's kludgey way of developing.

GLIBC_SRC=build/glibc/
GLIBC_BUILD=build/glibc-build/
HERE=$(shell pwd)

all: install


build:
	mkdir -p build/glibc build/glibc-build 
	git clone git://sourceware.org/git/glibc.git build/glibc
	#there seems to be a reasonable amount of movement in malloc (locks, at_fork) recently.
	#so some work to bring upto date ...
	cd build/glibc; git checkout 317b199b4aff8cfa27f2302ab404d2bb5032b9a4
	#git checkout origin/release/2.24/master

build/glibc-install: build
	mkdir -p build/glibc-install build/glibc-build 	
	cd build/glibc-build; ../glibc/configure  --prefix=${HERE}/build/glibc-install


update:
	cp malloc/*.[ch]  malloc/Makefile  ${GLIBC_SRC}/malloc


#N.B. if our malloc/Makefile does not optimize, now be a **very good time** to comment out 
#line 4 in `config.h` in the `glibc-build`.
compile: update
	$(MAKE) -C ${GLIBC_BUILD}

install: compile
	$(MAKE) -C ${GLIBC_BUILD} install

test:
	$(MAKE) -C ../glibc_tests  run



distclean:
	rm -rf build


