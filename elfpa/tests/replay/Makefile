CFLAGS= -Wall -I../../../mhooks -I../../include/ -ggdb

OBJDIR=../../obj
OBJ_GLOBS = $(addsuffix /*.o,${OBJDIR})
OBJS = $(sort $(wildcard ${OBJ_GLOBS}))

REPLAYDIR=../../../replay

OBJECTS = replay.o lphash.o mtreplay.o replaylib.o

%.o: ${REPLAYDIR}/%.c 
	${CC} ${CFLAGS} $< -c 

all: ${OBJECTS}
	${CC} ${CFLAGS} replay.o lphash.o replaylib.o ${OBJS} -o replay
	${CC} ${CFLAGS} mtreplay.o lphash.o replaylib.o ${OBJS} -lpthread -o mtreplay

clean:
	rm -rf *~ ${OBJECTS} replay mtreplay

test:  all
	./replay ../../../../analysis/data/yices_smt2_2668e3c6.txt

mtest:  all
	./mtreplay 4 ../../../../analysis/data/yices_smt2_2668e3c6.txt

check: test mtest

