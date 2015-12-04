
CFLAGS = -Wall  

OBJECTS = replay.o lphash.o

all: ${LIB} ${OBJECTS}
	${CC} ${CFLAGS} replay.o lphash.o -o replay


%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


clean:
	rm -rf *~ replay

test:  all
	./replay ../../analysis/data/yices_smt2_2668e3c6.txt
