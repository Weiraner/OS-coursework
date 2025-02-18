#Define the compiler, flags and other variables
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=gnu18
LOGIN = shiya
SUBMITPATH = ~cs537-1/handin/shiya/p3

.PHONY: all
all: wsh wsh-dbg
wsh: wsh.c
	$(CC) $(CFLAGS) -O2 -o $@ $^
wsh-dbg: wsh.c wsh.h
	$(CC) $(CFLAGS) -Og -ggdb -o $@ $^

.PHONY: clean
clean:
	rm -f wsh wsh-dbg

.PHONY: submit
submit:
	cp wsh wsh-dbg $(SUBMITPATH)

.PHONY: test
test: wsh
	./test.sh
