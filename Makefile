CC ?= gcc
CFLAGS ?= -Wall -Wextra -O0 $(shell pkg-config --cflags ncursesw)
LDLIBS ?= $(shell pkg-config --libs ncursesw)

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDLIBS)

clean:
	rm -f main

.PHONY: clean
