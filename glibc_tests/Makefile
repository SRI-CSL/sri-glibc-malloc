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

# Note Bene: if you get a file not found when trying to 
# run one of the tests; this hard coded loader version may 
# have changed!!
MY_LD=ld-2.23.90.so
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
  -Wl,--dynamic-linker=${SYSROOT}/lib/${MY_LD}



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

bug:
	./replay ../../analysis/data/QF_AUFBV_2668e3c6.smt2
	./replay ../../analysis/data/QF_AUFBV_80b4dce0.smt2


xalan:
	./replay ../../analysis/data/Xalan_base.gcc49-64bit.20160613204504

crypto:
	./replay ../../analysis/data/cryptominisat4-hook.out.txt

gcc:
	./replay ../../analysis/data/gcc_base.gcc49-64bit.20160613194243.txt

omnet:
	./replay ../../analysis/data/omnetpp_base.gcc49-64bit.20160613202413

perl:
	./replay ../../analysis/data/perlbench_base.gcc49-64bit.20160613192252

testreplay:
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt

mtestreplay:
	./mtreplay 4 ../../analysis/data/yices_smt2_2668e3c6.txt

#shaman
mstress8:
	./mtreplay 8 ../../analysis/data/yices_smt2_2668e3c6.txt

#shaman & pascali
mstress16:
	./mtreplay 16 ../../analysis/data/yices_smt2_2668e3c6.txt

#pascali
mstress32:
	./mtreplay 32 ../../analysis/data/yices_smt2_2668e3c6.txt
