#export DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=/Users/iam/Repositories/GitHub/HeapMetadata/src/frolloc/lib/libfrolloc.dylib


all:
	$(MAKE) -C ../frolloc
	cp ../frolloc/lib/libfrolloc.dylib .
	${CC} -Wall -I/Users/iam/Repositories/GitHub/HeapMetadata/src/frolloc/include helloworld.c  libfrolloc.dylib -o helloworld



clean:
	rm -f helloworld *~ libfrolloc.dylib

