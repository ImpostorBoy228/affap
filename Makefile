CC ?= gcc
CFLAGS ?= -Wall -Wextra -O0 $(shell pkg-config --cflags ncursesw)
LDLIBS ?= $(shell pkg-config --libs ncursesw)

TESTS_BIN = tests/tests

.PHONY: all tests clean

all: main

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDLIBS)

tests: tests/tests.c main.c tests/test_support.h
	$(CC) $(CFLAGS) -o $(TESTS_BIN) tests/tests.c $(LDLIBS)
	./$(TESTS_BIN)

clean:
	rm -f main $(TESTS_BIN) tests/*.gcda tests/*.gcno tests/*.gcov *.gcda *.gcno *.gcov