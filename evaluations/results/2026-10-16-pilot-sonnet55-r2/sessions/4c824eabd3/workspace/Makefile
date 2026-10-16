CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g

test: build/test_ir_parse
	./build/test_ir_parse

asan:
	mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all \
		src/ir_parse.c tests/test_ir_parse.c -o build/test_asan
	./build/test_asan

build/test_ir_parse: src/ir_parse.c src/ir_parse.h tests/test_ir_parse.c
	mkdir -p build
	$(CC) $(CFLAGS) src/ir_parse.c tests/test_ir_parse.c -o $@

clean:
	rm -rf build

.PHONY: test asan clean
