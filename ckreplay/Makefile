
CFLAGS= -Wall  -I../mhooks

CKCFLAGS= ${CFLAGS} -DUSE_LFPA_PREFIX  -I../lfpa

LDFLAGS = -L../lfpa -llfpa -lck

OBJECTS = replay.o test.o mtreplay.o ckallocator.o cktable.o

all: ${OBJECTS}
	${CC} ${CFLAGS} ckallocator.o cktable.o test.o -o test ${LDFLAGS} 
	${CC} ${CFLAGS} ckallocator.o cktable.o replay.o replaylib.c -o replay ${LDFLAGS} 
	${CC} ${CFLAGS} ckallocator.o cktable.o mtreplay.o replaylib.c -lpthread -o mtreplay ${LDFLAGS} 


%.o: %.c %.h 
	${CC} ${CKCFLAGS} $< -c 



run:
	LD_LIBRARY_PATH=../lfpa ./test

test:  all
	LD_LIBRARY_PATH=../lfpa ./replay ../../analysis/data/yices_smt2_2668e3c6.txt

mtest:  all
	LD_LIBRARY_PATH=../lfpa ./mtreplay 4 ../../analysis/data/yices_smt2_2668e3c6.txt

clean:
	rm -f test ${OBJECTS} replaylib.o
