CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined

test: build/test_ir_parser
	./build/test_ir_parser corpus

build/test_ir_parser: ir_parser.c ir_parser.h tests/test_ir_parser.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ ir_parser.c tests/test_ir_parser.c

clean:
	rm -rf build

.PHONY: test clean
