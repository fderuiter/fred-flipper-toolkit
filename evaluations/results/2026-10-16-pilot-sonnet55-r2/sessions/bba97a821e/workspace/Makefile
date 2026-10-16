CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O1 -g
SAN = -fsanitize=address,undefined -fno-sanitize-recover=all

.PHONY: test clean
test: test_ir_parse
	./test_ir_parse

test_ir_parse: test_ir_parse.c ir_parse.c ir_parse.h
	$(CC) $(CFLAGS) $(SAN) -DCORPUS_DIR='"corpus"' -o $@ test_ir_parse.c ir_parse.c

irdump: irdump.c ir_parse.c ir_parse.h
	$(CC) $(CFLAGS) -o $@ irdump.c ir_parse.c

clean:
	rm -f test_ir_parse irdump
