

SYSROOT=${HOME}/Repositories/HeapMetadata/Variants/glibc-install

CFLAGS = -Wall  -g -O0 \
  -L${SYSROOT}/usr/lib \
  -I${SYSROOT}/include \
  --sysroot=${SYSROOT} \
  -Wl,-rpath=${SYSROOT}/lib \
  -Wl,--dynamic-linker=${SYSROOT}/lib/ld-2.22.90.so 




TESTS = stest0 stest1 stest2

%.o: %.c %.h 
	$(CC) $(CFLAGS) $< -c 

all: $(TESTS)

stest0: stest0.c
	$(CC) $(CFLAGS) stest0.c  -o  $@

stest1: stest1.c
	$(CC) $(CFLAGS) stest1.c  -o  $@

stest2: stest2.c
	$(CC) $(CFLAGS) stest2.c  -o  $@

clean:
	rm -f $(TESTS) 


run:  clean all 
	./stest0
	./stest1
	./stest2

