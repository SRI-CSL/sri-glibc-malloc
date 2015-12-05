all: mhook.so

mhook.so: mhook.c
	gcc -shared -fPIC -Wall -o mhook.so mhook.c


hell:
	gcc -g -shared -fPIC -Wall -o mhook.so mhook.c
	gcc -g hello.c -c
	gcc -g hello.o  ./mhook.so -o hello

hooks:
	gcc -g -o hooks hooks.c

clean: 
	rm -rf mhook.so hello hooks
