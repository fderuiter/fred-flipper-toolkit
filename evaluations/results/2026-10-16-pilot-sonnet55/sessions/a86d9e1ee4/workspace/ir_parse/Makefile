CFLAGS ?= -std=c99 -Wall -Wextra -g -fsanitize=address,undefined

test: test_ir_parse
	./test_ir_parse ../corpus

test_ir_parse: ir_parse.c ir_parse.h test_ir_parse.c
	$(CC) $(CFLAGS) ir_parse.c test_ir_parse.c -o $@

clean:
	rm -f test_ir_parse
