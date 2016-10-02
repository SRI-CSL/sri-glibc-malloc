#ian's kludgey way of developing.

HERE=$(shell pwd)
GLIBC_SRC=$(HERE)/build/glibc/

ifeq ($(shell whoami),vagrant)
#building uses hard links (can't be on the shared drive)
GLIBC_BUILD=/home/vagrant/glibc-build/
GLIBC_INSTALL=/home/vagrant/glibc-install/
else
GLIBC_BUILD=$(HERE)/build/glibc-build/
GLIBC_INSTALL=$(HERE)/build/glibc-install
endif

all: install


build:
	mkdir -p build/glibc build/glibc-build 
	git clone git://sourceware.org/git/glibc.git build/glibc
#there seems to be a reasonable amount of movement in malloc (locks, at_fork) recently.
#so some work to bring upto date ...
	cd build/glibc; git checkout 317b199b4aff8cfa27f2302ab404d2bb5032b9a4
#probably should move towards a release or have branches per release.
#git checkout origin/release/2.24/master

build/glibc-install: build
	mkdir -p build/glibc-install build/glibc-build 	
	cd build/glibc-build; ../glibc/configure  --prefix=$(GLIBC_INSTALL)


update:
	cp src/sri-glibc/malloc/*.[ch]  malloc/Makefile  $(GLIBC_SRC)/malloc


#N.B. if our malloc/Makefile does not optimize, now be a **very good time** to comment out 
#line 4 in `config.h` in the `glibc-build`.
compile: update
	$(MAKE) -C $(GLIBC_BUILD)

install: compile
	$(MAKE) -C $(GLIBC_BUILD) install

test: compile
	$(MAKE) -C src/sri-glibc/glibc_tests  run



distclean:
	rm -rf build


