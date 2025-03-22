CC=gcc

aesdsocket: aesdsocket.o
	$(CROSS_COMPILATION)$(CC) aesdsocket.o -o aesdsocket 

aesdsocket.o:
	$(CROSS_COMPILATION)$(CC) -c aesdsocket.c -o aesdsocket.o

all: aesdsocket

clean:
	rm -f *.o aesdsocket
