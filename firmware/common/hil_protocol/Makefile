CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -O2

.PHONY: all test clean
all: test_hil_protocol

test_hil_protocol: src/hil_protocol.c tests/test_hil_protocol.c include/hil_protocol.h
	$(CC) $(CFLAGS) -Iinclude src/hil_protocol.c tests/test_hil_protocol.c -o $@

test: test_hil_protocol
	./test_hil_protocol

clean:
	rm -f test_hil_protocol
