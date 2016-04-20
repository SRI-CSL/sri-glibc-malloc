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

CLFLAGS		= -lpthread -lm
CFLAGS		= -D$(ASM) -D_GNU_SOURCE -D_REENTRANT  #-DDEBUG $(NOBUILTINS) 

GCC_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
GCC_OPT		= -ggdb -O3 # -DDEBUG

CLANG_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
CLANG_OPT	= -O3 -ggdb #-DDEBUG

ifeq ($(CC), gcc)
	OPT = $(GCC_OPT)
	CFLAGS += $(GCC_CFLAGS)  -Iinclude-$(ASM)
	CFLAGS+= -DNDEBUG
endif

ifeq ($(CC), clang)
	OPT = $(CLANG_OPT)
	CFLAGS += $(CLANG_CFLAGS)  -Iinclude-$(ASM)
	CFLAGS+= -DNDEBUG
endif

# Rules

all:	${LIBLFPA} ${LIBCKLFPA} replay mtreplay test128 

clean:
	rm -f *.o   ${LIBLFPA} ${LIBCKLFPA} replay mtreplay test128 

malloc.o:	malloc.h malloc.c include-$(ASM)/atomic.h include-$(ASM)/queue.h
		$(CC) $(CFLAGS) $(OPT) -Iinclude-$(ASM) -c malloc.c

lfpamalloc.o:	malloc.h malloc.c include-$(ASM)/atomic.h include-$(ASM)/queue.h
		$(CC) $(CFLAGS) -DUSE_LFPA_PREFIX $(OPT) -Iinclude-$(ASM) -c malloc.c -o lfpamalloc.o

${LIBLFPA}:	malloc.o
		$(CC) $(CFLAGS) ${LIBFLAGS} $(OPT) malloc.o -o ${LIBLFPA} $(CLFLAGS) -shared

${LIBCKLFPA}:	lfpamalloc.o
		$(CC) $(CFLAGS) ${LIBFLAGS} $(OPT) lfpamalloc.o -o ${LIBCKLFPA} $(CLFLAGS) -shared

test128:	include-$(ASM)/atomic.h  test128.c
		$(CC) $(CFLAGS) $(OPT) -Iinclude-$(ASM) test128.c -o test128 -lpthread


# Replay stuff
MALLOCOBJ = malloc.o

lphash.o: ../lphash/lphash.c
	$(CC) $(CFLAGS) $(OPT) -I../lphash ../lphash/lphash.c -c

replaylib.o:  replaylib.c
	$(CC) $(CFLAGS) $(OPT) -I../lphash -I../mhooks replaylib.c -c

replay: replay.c $(MALLOCOBJ) lphash.o replaylib.o 
	$(CC) $(CFLAGS) $(OPT) -I../lphash  replay.c $(MALLOCOBJ)  lphash.o replaylib.o -lpthread  -o  $@

mtreplay: mtreplay.c $(MALLOCOBJ) lphash.o replaylib.o 
	$(CC) $(CFLAGS) $(OPT) -I../lphash  mtreplay.c $(MALLOCOBJ)  lphash.o replaylib.o -lpthread  -o  $@

SMT2 = 2668e3c6 d6c91c1f da2ad2d3

testreplay: replay
	for smt in $(SMT2) ; do \
	    echo "running $$smt" ; \
	    echo "./replay ../../analysis/data/yices_smt2_$$smt.txt" ; \
	    ./replay ../../analysis/data/yices_smt2_$$smt.txt ; \
	done

mtestreplay: mtreplay
	./mtreplay 2 ../../analysis/data/yices_smt2_2668e3c6.txt

INTERVAL = `seq 1 10`

mstress: mtreplay
	for number in ${INTERVAL} ; do \
	      echo "Run $$number" ; \
	      ./mtreplay 2 ../../analysis/data/yices_smt2_2668e3c6.txt ;  \
	done

#for shaman
mstress8: mtreplay
	for number in ${INTERVAL} ; do \
	      echo "Run $$number" ; \
	      ./mtreplay 8 ../../analysis/data/yices_smt2_2668e3c6.txt ;  \
	done

#for pascali
mstress16: mtreplay
	for number in ${INTERVAL} ; do \
	      echo "Run $$number" ; \
	      ./mtreplay 16 ../../analysis/data/yices_smt2_2668e3c6.txt ;  \
	done
