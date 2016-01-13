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

CC		= gcc

CLFLAGS		= -lpthread -lm
CFLAGS		= -D$(ASM) -D_GNU_SOURCE -D_REENTRANT #-DDEBUG

GCC_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
GCC_OPT		= -O3 -ggdb #-DDEBUG

CLANG_CFLAGS	= -Wall $(BITS) -fno-strict-aliasing $(FPIC)
CLANG_OPT	= -O3 -ggdb #-DDEBUG

ICC_CFLAGS	= -Wall -wd279 -wd981 -wd1418 -wd1469 -wd383 -wd869 -wd522 -wd810 -wd1684
ICC_OPT		= -O3 -pipe -finline-functions -fomit-frame-pointer #-march=pentium4 -mcpu=pentium4 

ifeq ($(CC), gcc)
	OPT = $(GCC_OPT)
	CFLAGS += $(GCC_CFLAGS)
endif

ifeq ($(CC), clang)
	OPT = $(CLANG_OPT)
	CFLAGS += $(CLANG_CFLAGS)
endif

ifeq ($(CC), icc)
	OPT = $(ICC_OPT)
	CFLAGS += $(ICC_CFLAGS)
endif

# Rules

all:	libmichael.so 

clean:
	rm -f *.o *.so

michael.o:	michael.h michael.c include-$(ASM)/atomic.h include-$(ASM)/queue.h
		$(CC) $(CFLAGS) $(OPT) -Iinclude-$(ASM) -c michael.c

libmichael.so:	michael.o
		$(CC) $(CFLAGS) $(OPT) michael.o -o libmichael.so $(CLFLAGS) -shared

