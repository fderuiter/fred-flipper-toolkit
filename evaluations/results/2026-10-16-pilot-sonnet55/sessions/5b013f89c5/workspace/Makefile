CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wconversion -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all

.PHONY: test clean
test: build/test_ir_parser
	build/test_ir_parser corpus

build/test_ir_parser: ir_parser/ir_parser.c tests/test_ir_parser.c ir_parser/ir_parser.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ ir_parser/ir_parser.c tests/test_ir_parser.c

clean:
	rm -rf build
