# N.B You need to add some linux headers/libs to sysroot
# to get the replay stuff to compile.
#
# cd ../../Variants/glibc-install/include
# ln -s /usr/include/linux . 
# ln -s /usr/include/x86_64-linux-gnu/asm .
# ln -s /usr/include/asm-generic .
#
# cd ../../Variants/glibc-install/lib
# ln -s /lib/x86_64-linux-gnu/libgcc_s.so.1 .
#
#


ifeq ($(shell whoami),vagrant)
SYSROOT=${HOME}/glibc-install
else
SYSROOT=../../Variants/glibc-install
endif


CFLAGS = -Wall  -g -O0   -I../mhooks \
  -L${SYSROOT}/usr/lib \
  -I${SYSROOT}/include \
  --sysroot=${SYSROOT} \
  -Wl,-rpath=${SYSROOT}/lib \
  -Wl,--dynamic-linker=${SYSROOT}/lib/ld-2.22.90.so 



OBJECTS = replay.o lphash.o mtreplay.o replaylib.o

TESTS = stest0 stest1 stest2 replay mtreplay

%.o: %.c %.h 
	$(CC) $(CFLAGS) $< -c 

all: ${OBJECTS} $(TESTS) 

replay:
	${CC} ${CFLAGS} replay.o lphash.o replaylib.o -o replay

mtreplay:
	${CC} ${CFLAGS} mtreplay.o lphash.o replaylib.o -lpthread -o mtreplay

stest0: stest0.c
	$(CC) $(CFLAGS) stest0.c  -o  $@

stest1: stest1.c
	$(CC) $(CFLAGS) stest1.c  -o  $@

stest2: stest2.c
	$(CC) $(CFLAGS) stest2.c  -o  $@

clean:
	rm -f $(TESTS) $(OBJECTS) 


run:  clean all 
	./stest0
	./stest1
	./stest2
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt
	./mtreplay 4 ../../analysis/data/yices_smt2_2668e3c6.txt
