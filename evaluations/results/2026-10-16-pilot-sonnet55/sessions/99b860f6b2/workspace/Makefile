CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined

test: test_ir_parser
	./test_ir_parser

test_ir_parser: test_ir_parser.c ir_parser.c ir_parser.h
	$(CC) $(CFLAGS) -o $@ test_ir_parser.c ir_parser.c

clean:
	rm -f test_ir_parser
