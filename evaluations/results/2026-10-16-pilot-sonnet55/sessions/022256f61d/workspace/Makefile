CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined

build/test_ir_parser: src/ir_parser.c src/ir_parser.h tests/test_ir_parser.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/ir_parser.c tests/test_ir_parser.c

test: build/test_ir_parser
	./build/test_ir_parser corpus

clean:
	rm -rf build

.PHONY: test clean
