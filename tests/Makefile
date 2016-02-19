CFLAGS = -Wall 


TESTS = stest0 stest1 stest2 stest3

%.o: %.c %.h 
	$(CC) $(CFLAGS) $< -c 

all: $(TESTS)

stest0: stest0.c
	$(CC) $(CFLAGS) stest0.c  -o  $@

stest1: stest1.c
	$(CC) $(CFLAGS) stest1.c  -o  $@

stest2: stest2.c
	$(CC) $(CFLAGS) stest2.c  -o  $@

stest3: stest3.c
	$(CC) $(CFLAGS) stest3.c  -o  $@

clean:
	rm -f $(TESTS) 


