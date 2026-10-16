CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all

test: test_ir_parse
	./test_ir_parse

test_ir_parse: test_ir_parse.c ir_parse.c ir_parse.h
	$(CC) $(CFLAGS) -o $@ test_ir_parse.c ir_parse.c

ir_dump: ir_dump.c ir_parse.c ir_parse.h
	$(CC) $(CFLAGS) -o $@ ir_dump.c ir_parse.c

clean:
	rm -f test_ir_parse ir_dump

.PHONY: test clean
