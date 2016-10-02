#ian's kludgey way of developing.

HERE=$(shell pwd)
GLIBC_SRC=$(HERE)/build/glibc

OUR_SRC=$(HERE)/src/sri-glibc/malloc

ifeq ($(shell whoami),vagrant)
#building uses hard links (can't be on the shared drive)
GLIBC_BUILD=/home/vagrant/glibc-build
GLIBC_INSTALL=/home/vagrant/glibc-install
else
GLIBC_BUILD=$(HERE)/build/glibc-build
GLIBC_INSTALL=$(HERE)/build/glibc-install
endif

all: install


$(GLIBC_SRC):
	mkdir -p $(GLIBC_SRC)
	git clone git://sourceware.org/git/glibc.git $(GLIBC_SRC)
#there seems to be a reasonable amount of movement in malloc (locks, at_fork) recently.
#so some work to bring upto date ...
	cd $(GLIBC_SRC); git checkout 317b199b4aff8cfa27f2302ab404d2bb5032b9a4
#probably should move towards a release or have branches per release.
#git checkout origin/release/2.24/master

$(GLIBC_INSTALL): $(GLIBC_SRC)
	mkdir -p $(GLIBC_INSTALL)  $(GLIBC_BUILD)
	cd $(GLIBC_BUILD); ../glibc/configure  --prefix=$(GLIBC_INSTALL)


update:
	cp $(OUR_SRC)/*.[ch]  $(OUR_SRC)/Makefile  $(GLIBC_SRC)/malloc


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


