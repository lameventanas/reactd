#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void dprint(const char *fmt, ...) {
    va_list ap;
    struct timespec tp;

    if (0 == clock_gettime(CLOCK_REALTIME, &tp)) {
        struct tm tm;
        char s[30]; // [yyyy-mm-dd hh:mm:ss.xxxxxx]
        localtime_r(&tp.tv_sec, &tm);
        strftime(s, sizeof(s), "[%F %T.", &tm);
        // sprintf(s + strlen(s), "%03u] ", tp.tv_nsec / 1000000);
        sprintf(s + strlen(s), "%06u] ", tp.tv_nsec / 1000);
        fprintf(stderr, s);
    }

    va_start(ap,fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
