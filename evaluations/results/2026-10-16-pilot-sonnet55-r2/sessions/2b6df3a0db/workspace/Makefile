CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined

.PHONY: test clean
test: build/test_ir_parse
	./build/test_ir_parse corpus

build/test_ir_parse: ir_parse.c ir_parse.h tests/test_ir_parse.c
	mkdir -p build
	$(CC) $(CFLAGS) ir_parse.c tests/test_ir_parse.c -o $@

clean:
	rm -rf build
