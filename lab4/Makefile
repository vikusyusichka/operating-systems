CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g

OBJS = main.o kernel.o fs.o device.o

all: lab4

lab4: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

main.o: main.c fs.h kernel.h device.h
kernel.o: kernel.c kernel.h fs.h device.h
fs.o: fs.c fs.h device.h
device.o: device.c device.h

test: lab4
	rm -f test.img
	./lab4 test.img < demo.txt

clean:
	rm -f lab4 lab4.exe *.o test.img

.PHONY: all clean test
