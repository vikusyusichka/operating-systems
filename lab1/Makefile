CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g

OBJS = main.o allocator.o kernel.o tester.o

all: lab1

lab1: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

allocator.o: allocator.c allocator.h block.h kernel.h
kernel.o: kernel.c kernel.h
tester.o: tester.c tester.h allocator.h
main.o: main.c allocator.h tester.h

clean:
	rm -f lab1 lab1.exe *.o

.PHONY: all clean
