OS   =  $(shell uname)

ARCH ?= $(shell uname -m)


ifneq ($(ARCH), x86_64)
  error  "Got me just one ARCH today."
endif

LIBRARYNAME=libfrolloc

ifeq (Darwin, $(findstring Darwin, ${OS}))
LIB = ${LIBRARYNAME}.dylib
LIBFLAGS = -dynamiclib
LDFLAGS =
CFLAGS = -DDARWIN
else
LIB = ${LIBRARYNAME}.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
LDFLAGS = -lpthread
CFLAGS = -DLINUX
endif

#OPT= -O3 -ggdb
OPT=  -ggdb

#choice of lifo or fifo (fifo has not been tested yet).
LF_QUEUE = lifo

CFLAGS+= -Wall -m64 -fno-strict-aliasing -fPIC -I./include -I./arch/${ARCH} -I./src/${LF_QUEUE}
#CFLAGS+= -DNDEBUG
CFLAGS+= -DSRI_DEBUG 

SRC_GLOBS = $(addsuffix /*.c,src)
SRC = $(sort $(wildcard $(SRC_GLOBS)))



OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))
OBJ += obj/lf_queue.o

TARGET = lib/${LIB}

.PHONY: clean

all: ${TARGET}
	@echo Done.	


$(TARGET): $(OBJ) | lib
	$(CC) $(LIBFLAGS) ${OPT} -o $@ $(OBJ) 


obj:
	mkdir -p obj

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} ${OPT} $< -c -o $@

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} ${OPT} $< -c -o $@

obj/%.o: src/${LF_QUEUE}/%.c | obj
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



