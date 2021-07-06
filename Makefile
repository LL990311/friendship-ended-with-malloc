CC=gcc
CFLAGS=-fsanitize=address -Wall -Werror -std=gnu11 -g -lm -Wvla
# CFLAGS=-Wall -Werror -std=gnu11 -g -lm -Wvla -DDEBUG=1

tests: tests.c virtual_alloc.c
	$(CC) $(CFLAGS) $^ -o $@

run_tests: tests
	./tests

clean:
	rm ./tests
