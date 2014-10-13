.PHONY: all test

all: test

test: test.c ltable.c
	gcc -g ltable.c test.c -o test

