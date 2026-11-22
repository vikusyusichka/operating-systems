CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g

OBJS = main.o vm.o

all: lab3

lab3: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

vm.o: vm.c vm.h config.h
main.o: main.c vm.h config.h

clean:
	rm -f lab3 lab3.exe *.o

.PHONY: all clean
