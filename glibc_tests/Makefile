

#SYSROOT=${HOME}/Repositories/HeapMetadata/Variants/glibc-install

SYSROOT=../../Variants/glibc-install

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

all: $(TESTS) ${OBJECTS}

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
