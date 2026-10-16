CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined

test: build/test_ir_parse
	./build/test_ir_parse corpus

build/test_ir_parse: src/ir_parse.c src/ir_parse.h tests/test_ir_parse.c
	mkdir -p build
	$(CC) $(CFLAGS) src/ir_parse.c tests/test_ir_parse.c -o $@

clean:
	rm -rf build

.PHONY: test clean
