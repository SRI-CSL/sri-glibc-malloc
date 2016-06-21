
CFLAGS = -Wall  -I../mhooks -O2 -DNDEBUG

OBJECTS = replay.o lphash.o mtreplay.o replaylib.o

all: ${OBJECTS}
	${CC} ${CFLAGS} replay.o lphash.o replaylib.o -o replay
	${CC} ${CFLAGS} mtreplay.o lphash.o replaylib.o -lpthread -o mtreplay

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


clean:
	rm -rf *~ ${OBJECTS} replay mtreplay

test:  all
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt

mtest:  all
	./mtreplay 4 ../../analysis/data/yices_smt2_2668e3c6.txt
