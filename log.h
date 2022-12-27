#ifndef LOG_H
#define LOG_H

#include <syslog.h>
#include <stdio.h>

// default log facility:
#define LOG_FACILITY LOG_DAEMON

// log destinations:
#define LOG_TO_SYSLOG 0
#define LOG_TO_FILE   1
#define LOG_TO_STDOUT 2
#define LOG_TO_STDERR 3

static char *log_destinations[] = {
    "syslog", "file", "stdout", "stderr"
};

/* loglevels are the ones defined by syslog:
 *       LOG_EMERG      system is unusable
 *       LOG_ALERT      action must be taken immediately
 *       LOG_CRIT       critical conditions
 *       LOG_ERR        error conditions
 *       LOG_WARNING    warning conditions
 *       LOG_NOTICE     normal, but significant, condition
 *       LOG_INFO       informational message
 *       LOG_DEBUG      debug-level message
 */

// TODO: use this from syslog.h directly?
static char *log_levels[] = {
    "emergency", "alert", "critical", "error", "warning", "notice", "info", "debug"
};

typedef struct {
    int level;
    int dst;
    char *prefix; // used with strftime
    FILE *fh;
} log_h;

log_h *log_open(int dst, int level, char *prefix, char *file);
#define logw log_write
void log_write(log_h *logh, int level, const char *fmt, ...);
void log_close(log_h *logh);

// returns dst as int
int logdst_int(char *dst, int fallback);

// returns dst as string
char *logdst_str(int dst);

// returns loglevel as int
int loglevel_int(char *level, int fallback);

// returns loglevel as string
char *loglevel_str(int level);

#endif