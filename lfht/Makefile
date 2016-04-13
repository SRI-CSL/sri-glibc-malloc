
CFLAGS= -Wall  -I./include

SRC = src/lfht.c

OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))

all: ${OBJ}

obj:
	mkdir -p obj

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} $< -c -o $@

lib:
	mkdir -p lib


clean:
	rm -f *~ *.o
	rm -rf obj lib



