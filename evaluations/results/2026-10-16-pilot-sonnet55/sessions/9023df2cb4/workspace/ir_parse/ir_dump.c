/* PC tool: ir_dump FILE.ir -- lists buttons like the FAP would show them. */
#include "ir_parse.h"

#include <stdio.h>

static IrParser parser;

int main(int argc, char** argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s FILE.ir\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if(!f) {
        perror(argv[1]);
        return 2;
    }
    char chunk[512]; /* same pattern a FAP uses with storage reads */
    size_t n;
    ir_parser_init(&parser);
    while(parser.status == IrOk && (n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        ir_parser_feed(&parser, chunk, n);
    fclose(f);
    ir_parser_finish(&parser);

    for(size_t i = 0; i < parser.count; i++) {
        const IrButton* b = &parser.buttons[i];
        if(b->type == IrTypeParsed)
            printf("%-24s parsed  %s\n", b->name, b->protocol);
        else
            printf("%-24s raw     %lu Hz  %u timings\n", b->name, (unsigned long)b->frequency, b->timing_count);
    }
    if(parser.status != IrOk) {
        printf("error line %lu: %s\n", (unsigned long)parser.error_line, ir_status_str(parser.status));
        return 1;
    }
    return 0;
}
