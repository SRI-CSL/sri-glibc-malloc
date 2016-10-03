#ian's kludgey way of developing.

HERE=$(shell pwd)

OUR_SRC=$(HERE)/src/sri-glibc/malloc

#building or installing uses hard links (can't be on the shared drive)
ifeq ($(shell whoami),vagrant)
BUILD=/home/vagrant/build
else
BUILD=$(HERE)/build
endif

GLIBC_SRC=$(BUILD)/glibc
GLIBC_BUILD=$(BUILD)/glibc-build
GLIBC_INSTALL=$(BUILD)/glibc-install


all: install

$(GLIBC_SRC):
	mkdir -p $(GLIBC_SRC)
	git clone git://sourceware.org/git/glibc.git $(GLIBC_SRC)
#there seems to be a reasonable amount of movement in malloc (locks, at_fork) recently.
#so some work to bring upto date ...
	cd $(GLIBC_SRC); git checkout 317b199b4aff8cfa27f2302ab404d2bb5032b9a4
#probably should move towards a release or have branches per release.
#git checkout origin/release/2.24/master

$(GLIBC_BUILD): $(GLIBC_SRC)
	mkdir -p $(GLIBC_INSTALL)  $(GLIBC_BUILD)
	cd $(GLIBC_BUILD); $(GLIBC_SRC)/configure  --prefix=$(GLIBC_INSTALL)
	cd $(GLIBC_INSTALL)/include; ln -s /usr/include/linux . ; ln -s /usr/include/x86_64-linux-gnu/asm . ;  ln -s /usr/include/asm-generic .
	cd $(GLIBC_INSTALL)/lib; ln -s /lib/x86_64-linux-gnu/libgcc_s.so.1 .




update: $(GLIBC_BUILD)
	cp $(OUR_SRC)/*.[ch]  $(OUR_SRC)/Makefile  $(GLIBC_SRC)/malloc


#N.B. if our $(OUR_SRC)/Makefile does not optimize, now be a **very good time** to comment out 
#line 4 in `config.h` in the $(GLIBC_BUILD) directory.
compile: update
	$(MAKE) -C $(GLIBC_BUILD)

install: compile
	$(MAKE) -C $(GLIBC_BUILD) install

check: compile
	$(MAKE) -C src/glibc_tests  check



distclean:
	rm -rf build

