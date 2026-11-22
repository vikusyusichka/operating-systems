CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g

OBJS = main.o allocator.o bitmap.o radix.o kernel.o tester.o

all: lab2

lab2: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

allocator.o: allocator.c allocator.h bitmap.h config.h kernel.h radix.h
bitmap.o: bitmap.c bitmap.h kernel.h
radix.o: radix.c radix.h kernel.h
kernel.o: kernel.c kernel.h
tester.o: tester.c tester.h allocator.h
main.o: main.c allocator.h tester.h

clean:
	rm -f lab2 lab2.exe *.o

.PHONY: all clean
