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
else
LIB = ${LIBRARYNAME}.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
LDFLAGS = -lpthread
endif

OPT= -O3 -ggdb


CFLAGS= -Wall -m64 -fno-strict-aliasing -fPIC -I./include -I./arch/${ARCH}
#CFLAGS+= -DNDEBUG

SRC_GLOBS = $(addsuffix /*.c,src)
SRC = $(sort $(wildcard $(SRC_GLOBS)))



OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))

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

lib:
	mkdir -p lib


UNIT_TESTS=./tests/unit

check:
	make -C ${UNIT_TESTS} check


clean:
	rm -f *~ *.o
	rm -rf obj lib



