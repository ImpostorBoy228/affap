CC ?= gcc
CFLAGS ?= -Wall -std=c23 -Wextra -O0 $(shell pkg-config --cflags ncursesw)
LDLIBS ?= $(shell pkg-config --libs ncursesw)

.PHONY: all clean

all: main

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDLIBS)

clean:
	rm -f main
