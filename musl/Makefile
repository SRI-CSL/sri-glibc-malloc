
CFLAGS = -Wall -fPIC -DNDEBUG -O3
LDFLAGS =


OS   =  $(shell uname)


ifeq (Darwin, $(findstring Darwin, ${OS}))
PLATFORM=Darwin
LIB=libmuslloc.dylib
LIBFLAGS = -dynamiclib
else
PLATFORM=Linux
LIB=libmuslloc.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
endif


SRC_GLOBS = $(addsuffix /*.c,src)
SRC = $(sort $(wildcard $(SRC_GLOBS)))

OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))

TARGET = lib/${LIB}

.PHONY: clean

all: ${TARGET}
	@echo Done.	

$(TARGET): $(OBJ) | lib
	${CC} ${LIBFLAGS} ${OPT} -o $@ ${OBJ}


obj:
	mkdir -p obj

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} ${OPT} $< -c -o $@

lib:
	mkdir -p lib

UNIT_TESTS=./tests/static
REPLAYS=./tests/replay

check: $(TARGET)
	make -C ${UNIT_TESTS} check
	make -C ${REPLAYS} check


clean:
	rm -f *~ *.o
	rm -rf obj lib
	make -C ${UNIT_TESTS} clean
	make -C ${REPLAYS} clean



