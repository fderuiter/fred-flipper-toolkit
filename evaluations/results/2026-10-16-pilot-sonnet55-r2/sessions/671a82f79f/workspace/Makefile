CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all

test: build/test_ir_parser
	./build/test_ir_parser corpus

build/test_ir_parser: src/ir_parser.c src/ir_parser.h tests/test_ir_parser.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc src/ir_parser.c tests/test_ir_parser.c -o $@

clean:
	rm -rf build

.PHONY: test clean
