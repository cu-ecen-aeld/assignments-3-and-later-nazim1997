CC=gcc

writer: writer.c
	$(CROSS_COMPILE)$(CC) -c writer.c -o writer.o
	$(CROSS_COMPILE)$(CC) writer.o -o writer    

all: writer

clean:
	rm -f writer writer.o
