/* PC tool: list the buttons of an .ir file the way the FAP would. Usage: irdump file.ir */
#include "ir_parse.h"

#include <stdio.h>

int main(int argc, char** argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s file.ir\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if(!f) {
        perror(argv[1]);
        return 2;
    }
    static ir_signal_t sig[IR_MAX_SIGNALS];
    ir_parser_t p;
    ir_parser_init(&p, sig, IR_MAX_SIGNALS);
    char buf[128]; /* same chunking a FAP would use */
    size_t n;
    while(p.status == IR_OK && (n = fread(buf, 1, sizeof buf, f)) > 0) ir_parser_feed(&p, buf, n);
    fclose(f);
    ir_status_t st = ir_parser_finish(&p);
    for(size_t i = 0; i < ir_parser_count(&p); i++) {
        const ir_signal_t* s = &sig[i];
        if(s->type == IR_SIGNAL_PARSED)
            printf("%-24s parsed  %s\n", s->name, s->protocol);
        else
            printf("%-24s raw     %u Hz, %u timings\n", s->name, (unsigned)s->frequency, (unsigned)s->timing_count);
    }
    if(st != IR_OK) {
        fprintf(stderr, "error: %s (line %u)\n", ir_status_str(st), (unsigned)p.error_line);
        return 1;
    }
    return 0;
}
