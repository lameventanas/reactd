#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>

#include "log.h"

log_h *log_open(int dst, int loglevel, char *prefix, char *file) {
    log_h *logh;

    logh = malloc(sizeof(log_h));
    logh->level = loglevel;
    logh->dst = dst;
    if (prefix != NULL)
        logh->prefix = strdup(prefix);
    else
        logh->prefix = NULL;

    if (dst == LOG_TO_FILE) {
        logh->fh = fopen(file, "a");
        if (logh->fh == NULL) {
            fprintf(stderr, "Error opening %s: %s\n", file, strerror(errno));
            logh->fh = fdopen(2, "a"); // log to STDERR instead
        }
    } else if (dst == LOG_TO_SYSLOG) {
        openlog(NULL, 0, LOG_DAEMON);
    } else if (dst == LOG_TO_STDOUT) { // stdout
        logh->fh = fdopen(1, "a");
    } else { // stderr (fallback option)
        logh->fh = fdopen(2, "a");
    }
    return logh;
}

void log_write(log_h *logh, int level, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    if (logh->dst == LOG_TO_SYSLOG) {
        vsyslog(LOG_FACILITY | level, fmt, ap);
    } else {
        // log to a file-handle: a file, stdout, or stderr obtained through fdopen()
        if (level <= logh->level) {
            if (logh->prefix != NULL) {
                char s[128];
                struct tm tms;
                time_t t = time(NULL);

                localtime_r(&t, &tms);
                strftime(s, sizeof(s), logh->prefix, &tms);
                fprintf(logh->fh, s);
            }
            vfprintf(logh->fh, fmt, ap);
            fprintf(logh->fh, "\n");
        }
    }
    va_end(ap);
}

void log_close(log_h *logh) {
    if (logh->dst == LOG_TO_SYSLOG) {
        closelog();
    } else {
        fclose(logh->fh);
    }
    if (logh->prefix)
        free(logh->prefix);
    free(logh);
}

int logdst_int(char *dst, int fallback) {
    if (dst == NULL)
        return fallback;
    if (!strcmp(dst, "syslog")) {
        return LOG_TO_SYSLOG;
    } else     if (!strcmp(dst, "file")) {
        return LOG_TO_FILE;
    } else     if (!strcmp(dst, "stdout")) {
        return LOG_TO_STDOUT;
    } else     if (!strcmp(dst, "stderr")) {
        return LOG_TO_STDERR;
    }
    return fallback;
}

char *logdst_str(int dst) {
    return log_destinations[dst];
}

int loglevel_int(char *level, int fallback) {
    if (!strcmp(level, "emerg")) {
        return LOG_EMERG;
    } else     if (!strcmp(level, "alert")) {
        return LOG_ALERT;
    } else     if (!strcmp(level, "crit") || !strcmp(level, "critical")) {
        return LOG_CRIT;
    } else     if (!strcmp(level, "err") || !strcmp(level, "error")) {
        return LOG_ERR;
    } else     if (!strcmp(level, "warn") || !strcmp(level, "warning")) {
        return LOG_WARNING;
    } else     if (!strcmp(level, "notice")) {
        return LOG_NOTICE;
    } else     if (!strcmp(level, "info")) {
        return LOG_INFO;
    } else     if (!strcmp(level, "debug")) {
        return LOG_DEBUG;
    }
    return fallback;
}

char *loglevel_str(int level) {
    return log_levels[level];
}
