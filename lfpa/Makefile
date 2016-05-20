# What architecture are we on?
# Options:
# 	- ppc64 (IBM PowerPC, 64-bit)
#	- x86 (Intel x86, 32-bit)
#	- x86_64 (Intel x86, 64-bit)
#	- ia64 (Intel Itanium, 64-bit)	

ASM	= x86_64

ifeq ($(ASM), ppc64)
	BITS = -m64
	FPIC = -fPIC
endif
ifeq ($(ASM), x86)
	BITS = -m32
endif
ifeq ($(ASM), x86_64)
	BITS = -m64
	FPIC = -fPIC
endif
ifeq ($(ASM), ia64)
	FPIC = -fPIC
endif

OS   =  $(shell uname)

ifeq (Darwin, $(findstring Darwin, ${OS}))
#  DARWIN
LIBLFPA = libmalloc.dylib
LIBCKLFPA = liblfpa.dylib
LIBFLAGS = -dynamiclib
else
# LINUX
LIBLFPA = libmalloc.so
LIBCKLFPA = liblfpa.so
LIBFLAGS = -shared -Wl,-soname,${LIB}
endif


NOBUILTINS      =  -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc  -fno-builtin-memcpy

CC		= gcc

CFLAGS = -Wall ${BITS} ${FPIC} -fno-strict-aliasing -I./include -I./arch/${ARCH}


CLFLAGS		= -lpthread -lm
CFLAGS		+= -D$(ASM) -D_GNU_SOURCE -D_REENTRANT  #-DDEBUG $(NOBUILTINS) 

GCC_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
GCC_OPT		= -ggdb -O3 # -DDEBUG

CLANG_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
CLANG_OPT	= -O3 -ggdb #-DDEBUG

ifeq ($(CC), gcc)
	OPT = $(GCC_OPT)
	CFLAGS += $(GCC_CFLAGS)  -Iarch/$(ASM)
	CFLAGS+= -DNDEBUG
endif

ifeq ($(CC), clang)
	OPT = $(CLANG_OPT)
	CFLAGS += $(CLANG_CFLAGS)  -Iarch/$(ASM)
	CFLAGS+= -DNDEBUG
endif


SRC_GLOBS = $(addsuffix /*.c,src)
SRC = $(sort $(wildcard $(SRC_GLOBS)))

OBJ = $(patsubst src/%.c, obj/%.o, $(SRC))

TARGETS = lib/${LIBLFPA} lib/${LIBCKLFPA}

all:	${TARGETS}
	@echo Done.


lib/${LIBLFPA}: obj/malloc.o | lib
		$(CC) $(CFLAGS) ${LIBFLAGS} $(OPT) obj/malloc.o -o lib/${LIBLFPA} $(CLFLAGS) -shared

lib/${LIBCKLFPA}: obj/lfpamalloc.o | lib 
		$(CC) $(CFLAGS) ${LIBFLAGS} $(OPT) obj/lfpamalloc.o -o lib/${LIBCKLFPA} $(CLFLAGS) -shared


obj:
	mkdir -p obj

obj/%.o: src/%.c | obj
	${CC} ${CFLAGS} ${OPT} $< -c -o $@

obj/lfpamalloc.o: include/malloc.h src/malloc.c arch/$(ASM)/atomic.h arch/$(ASM)/queue.h
	$(CC) $(CFLAGS) -DUSE_LFPA_PREFIX $(OPT) -Iarch/$(ASM) -c src/malloc.c -o obj/lfpamalloc.o


lib:
	mkdir -p lib


tests: all
	make -C tests/static
	make -C tests/replay
	make -C tests/unit ASM=${ASM}

check: tests
	make -C tests/static check
	make -C tests/replay check
	make -C tests/unit check






clean:
	rm -f *~ *.o 
	rm -rf obj lib
	make -C tests/static clean
	make -C tests/replay clean
	make -C tests/unit clean

malloc.o:	malloc.h malloc.c arch/$(ASM)/atomic.h arch/$(ASM)/queue.h
		$(CC) $(CFLAGS) $(OPT) -Iarch/$(ASM) -c malloc.c



