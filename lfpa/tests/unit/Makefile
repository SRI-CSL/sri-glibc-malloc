ROOT = ../..

CFLAGS = -Wall -m64 -fno-strict-aliasing -fPIC

test128:	${ROOT}/arch/$(ASM)/atomic.h  test128.c
		$(CC) $(CFLAGS) -I${ROOT}/arch/$(ASM) test128.c -o test128 -lpthread


clean:
	rm -f test128


check:
	./test128  16


