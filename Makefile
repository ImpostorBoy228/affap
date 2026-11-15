CC ?= gcc
CFLAGS ?= -Wall -std=c23 -Wextra -O3 -march=native -flto -DNDEBUG $(shell pkg-config --cflags ncursesw)
LDFLAGS ?= -flto
LDLIBS ?= $(shell pkg-config --libs ncursesw)

.PHONY: all clean

all: main

main: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o main main.c $(LDLIBS)

clean:
	rm -f main
