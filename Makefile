CC ?= gcc
CFLAGS ?= -Wall -Wextra -O0 $(shell pkg-config --cflags ncursesw)
LDLIBS ?= $(shell pkg-config --libs ncursesw)

TESTS_BIN = tests/tests

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDLIBS)

tests: tests/tests.c main.c
	$(CC) $(CFLAGS) -o $(TESTS_BIN) tests/tests.c $(LDLIBS)
	./$(TESTS_BIN)

coverage: CFLAGS += --coverage
coverage: tests/tests.c main.c
	rm -f tests/*.gcda tests/*.gcno tests/*.gcov
	cd tests && $(CC) $(CFLAGS) -o tests tests.c $(LDLIBS) && ./tests && gcov -b tests.c

clean:
	rm -f main $(TESTS_BIN) tests/*.gcda tests/*.gcno tests/*.gcov

.PHONY: clean tests coverage
