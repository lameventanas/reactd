#ifndef LOG_H
#define LOG_H

#include <syslog.h>
#include <stdio.h>

// default log facility:
#define LOG_FACILITY LOG_DAEMON

// logtype:
#define LOG_TO_FILE 0
#define LOG_TO_STDOUT 1
#define LOG_TO_STDERR 2
#define LOG_TO_SYSLOG 3

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

typedef struct {
    int level;
    int type;
    char *prefix; // used with strftime
    FILE *fh;
} log_h;

log_h *log_open(int logtype, int loglevel, char *prefix, char *file);
#define logw log_write
void log_write(log_h *logh, int level, const char *fmt, ...);
void log_close(log_h *logh);

// returns logtype from string
int logtype_str(char *logtype, int fallback);

// returns loglevel from string
int loglevel_str(char *loglevel, int fallback);

#endif