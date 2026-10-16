CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Wshadow -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined

test_ir_parser: test_ir_parser.c ir_parser.c ir_parser.h
	$(CC) $(CFLAGS) -o $@ test_ir_parser.c ir_parser.c

test: test_ir_parser
	./test_ir_parser corpus

clean:
	rm -f test_ir_parser

.PHONY: test clean
