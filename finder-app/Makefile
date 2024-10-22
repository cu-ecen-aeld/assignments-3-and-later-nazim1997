# CROSS_COMPILE ?= aarch64-none-linux-gnu-
CROSS_COMPILE ?=


writer: writer.c
	$(CROSS_COMPILE)gcc -c writer.c -o writer.o
	$(CROSS_COMPILE)gcc writer.o -o writer    

clean:
	rm -f writer writer.o
