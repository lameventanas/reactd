#ifndef REACTD_H
#define REACTD_H
#define DEFAULT_CONFIG "/etc/reactd.conf"
#define DEFAULT_PIDFILE "/var/run/reactd/reactd.pid"
#define DEFAULT_STATEFILE "/var/run/reactd/reactd.db"

#define VERSION "0.1"
// #define PROGRAM_NAME "reactd"
// #define EXIT_SUCCESS 0
// #define EXIT_FAILURE 1

#include <pcre.h>

#include <sys/inotify.h>
// for NAME_MAX:
#include <limits.h>

// for strchr:
#include <string.h>

// #define INOTIFY_EVENTS        (IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT)
#define INOTIFY_EVENTS        IN_MODIFY
#define INOTIFY_NEVENTS_NUM    4

#include <poll.h>

#include <assert.h>

#include "log.h"
#include "keylist.h"
#include "ring.h"
#include "pcre_subst.h"

#define MAX_RE_CAPTURES 10 // maximum number of RE captures, including \0

// params to tail log file
// #define INOTIFY_EVENTS IN_ALL_EVENTS
// time in microseconds to wait after event is detected, to let more log entries accumulate
// higher values reduce CPU usage
#define EVENT_REACT_TIME 250000
#define LOG_CREATE_SCAN_INTERVAL 2000 // in milliseconds
// #define USE_MMAP
#define RESET_GUARD_TIME 250 // in milliseconds

typedef struct {
    char *str;
    char *cmd;
    char *reset_cmd;
    unsigned int trigger_time; // in seconds
    unsigned int reset_time;
    pcre *re;
    pcre_extra *re_studied;
    pcre_subst *key;
    keylist *hits; // key => ring where key is presumably the IP
    unsigned int trigger_cnt; // max number of hits (size of ring)
} re;

typedef struct {
    char *name;
    re *re; // last position marked by null
    off_t pos; // position read until now
    int watchfd; // unique watch descriptor associated with this file (as returned from inotify_add_watch)
} tfile; // tailed file type

typedef struct {
    unsigned int version_major;
    unsigned int version_minor;
    char *pidfile;
    char *logging;
    char *logfile;
    char *loglevel;
    char *logprefix;
} tglobal_cfg;
// tglobal_cfg global_cfg;

// defined in reactd_conf.y
extern tglobal_cfg cfg;
extern int parse_config(char *filename);

// these are defined in reactd.c
// extern keylist *fmon_cfgs; // fmon_cfg keylist
// extern tfmon *fmons; // fmons list ended by null
extern log_h *logh; // log handle
extern struct pollfd pollwatch; // used to store inotify file descriptor and to poll it for read events
extern int unwatchedfiles; // number of unwatched files (files in the config file but that are not being watched because they don't exist)
// extern int filenum; // number of files we are watching

#endif