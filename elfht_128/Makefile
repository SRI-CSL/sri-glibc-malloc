	OS   =  $(shell uname)

ARCH ?= $(shell uname -m)


ifneq ($(ARCH), x86_64)
  error  "Got me just one ARCH today."
endif


LIBRARYNAME=liblfht

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LIB = ${LIBRARYNAME}.dylib
LDFLAGS = -g
LIBFLAGS = -dynamiclib ${LDFLAGS}
else
# LINUX
LIB = ${LIBRARYNAME}.so
LDFLAGS = -lpthread -ggdb
LIBFLAGS = -shared -Wl,-soname,${LIB} ${LDFLAGS}
endif


CFLAGS= -Wall -fPIC -I./include -I./arch/${ARCH} -ggdb

SRC_GLOBS = $(addsuffix /*.c,src)
SRC = $(sort $(wildcard $(SRC_GLOBS)))



OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))

TARGET = lib/${LIB}

.PHONY: clean

all: ${TARGET}
	@echo Done.	


$(TARGET): $(OBJ) | lib
	$(CC) $(LIBFLAGS) -o $@ $(OBJ) 


obj:
	mkdir -p obj

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} $< -c -o $@

lib:
	mkdir -p lib


UNIT_TESTS=./tests/unit

check:  ${TARGET}
	make -C ${UNIT_TESTS} check


clean:
	rm -f *~ *.o
	rm -rf obj lib
	make -C ${UNIT_TESTS} clean



