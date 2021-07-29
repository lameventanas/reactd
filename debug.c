#include <time.h>
#include <stdarg.h>
#include <stdio.h>

static time_t dprint_time = 0;

void dprint_init() {
    dprint_time = time(NULL);
}

void dprint(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%04d: ", time(NULL) - dprint_time);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}