#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "log.h"

log_h *log_open(int logtype, int loglevel, char *prefix, char *file) {
    log_h *logh;

    logh = malloc(sizeof(log_h));
    logh->level = loglevel;
    logh->type = logtype;
    if (prefix != NULL)
        logh->prefix = strdup(prefix);
    else
        logh->prefix = NULL;

    if (logtype == LOG_TO_FILE) {
        logh->fh = fopen(file, "a");
        if (logh->fh == NULL) {
            fprintf(stderr, "Error opening %s: %s\n", file, strerror(errno));
            logh->fh = fdopen(2, "a"); // log to STDERR instead
        }
    } else if (logtype == LOG_TO_SYSLOG) {
        openlog(NULL, 0, LOG_DAEMON);
    } else { // stdout or stderr
        logh->fh = fdopen(logtype, "a");
    }
    return logh;
}

void log_write(log_h *logh, int level, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    if (logh->type == LOG_TO_SYSLOG) {
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
    if (logh->type == LOG_TO_SYSLOG) {
        closelog();
    } else {
        fclose(logh->fh);
    }
    if (logh->prefix)
        free(logh->prefix);
    free(logh);
}

int logtype_str(char *logtype, int fallback) {
    if (logtype == NULL)
        return fallback;
    if (!strcmp(logtype, "syslog")) {
        return LOG_TO_SYSLOG;
    } else     if (!strcmp(logtype, "file")) {
        return LOG_TO_FILE;
    } else     if (!strcmp(logtype, "stdout")) {
        return LOG_TO_STDOUT;
    } else     if (!strcmp(logtype, "stderr")) {
        return LOG_TO_STDERR;
    }
    return fallback;
}

int loglevel_str(char *logtype, int fallback) {
    if (!strcmp(logtype, "emerg")) {
        return LOG_EMERG;
    } else     if (!strcmp(logtype, "alert")) {
        return LOG_ALERT;
    } else     if (!strcmp(logtype, "crit") || !strcmp(logtype, "critical")) {
        return LOG_CRIT;
    } else     if (!strcmp(logtype, "err") || !strcmp(logtype, "error")) {
        return LOG_ERR;
    } else     if (!strcmp(logtype, "warn") || !strcmp(logtype, "warning")) {
        return LOG_WARNING;
    } else     if (!strcmp(logtype, "notice")) {
        return LOG_NOTICE;
    } else     if (!strcmp(logtype, "info")) {
        return LOG_INFO;
    } else     if (!strcmp(logtype, "debug")) {
        return LOG_DEBUG;
    }
    return fallback;
}
