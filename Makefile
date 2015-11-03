

CFLAGS = -Wall 



all: linhash.o meta.o  memcxt.o dynahash.o

%.o: %.c %.h 
	${CC} ${CFLAGS} $< -c 


clean:
	rm -f *~ *.o



