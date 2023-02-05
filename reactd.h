#ifndef REACTD_H
#define REACTD_H

#define VERSION "0.1"
#define PROGRAM_NAME "reactd"
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define DEFAULT_LOGDST LOG_TO_SYSLOG
#define DEFAULT_LOGLEVEL LOG_INFO

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
#include "avl.h"
#include "ring.h"
#include "pcre_subst.h"

// params to tail log file
// #define INOTIFY_EVENTS IN_ALL_EVENTS
// time in microseconds to wait after event is detected, to let more log entries accumulate
// higher values reduce CPU usage
#define EVENT_REACT_TIME 250000
#define LOG_CREATE_SCAN_INTERVAL 2000 // in milliseconds
// #define USE_MMAP

#define POLL_FD_INOTIFY 0

typedef struct {
    pcre_subst **args; // pcre_subst template for cmd and arguments (0=cmd)
    unsigned int cnt;  // number of template strings in args
} tcmd;

// regex
typedef struct {
    char *str;
    tcmd *cmd;
    tcmd *reset_cmd;
    unsigned int trigger_time; // in seconds
    unsigned int trigger_cnt; // max number of hits (size of ring)
    unsigned int reset_time;
    pcre *re;
    pcre_extra *studied;      // result of pcre_study()
    unsigned int capture_cnt; // number of capture groups, including one for \0
    pcre_subst *key;
    struct avl_table *hitlist; // avl of keyhits, where key is presumably the IP
} re;

// tailed file
typedef struct {
    char *name;          // filename
    re **re;             // re's
    unsigned int re_cnt; // number of re's
    off_t pos;           // position read until now
    int watchfd;         // unique watch descriptor associated with this file (as returned from inotify_add_watch)
} tfile;

#ifndef SYSTEMD
#define POLL_FD_CNT 1
#else
#define POLL_FD_CNT 2
#define POLL_FD_JOURNAL 1
// journal filters
typedef struct {
    char **fields;
    char **values;
    unsigned int cnt;
} tj_filters;

// tailed journal
typedef struct {
    tj_filters *filters;  // journal filters
    re **re;              // re's
    unsigned int re_cnt;  // number of re's
} tjournal;
#endif

// avl tuple node for hitlist in re's for both files and journal
typedef struct {
    char *key;  // key string used to locate hitlist in avl tree
    ring *hits; // ring with time of each hit
} keyhits;

// to remember extra env vars to use when running reset commands
typedef struct {
    char **names;
    char **values;
    unsigned int cnt; // number of names and values
} tenv;

// reset item in resets
typedef struct {
    char *key;
    keyhits *hits; // pointer to keyhits avl (NULL if there is no trigger period set)
    char *source;  // source where match occurred (used to set REACT_SOURCE) (must not be freed, it's a pointer to tfile->name or "journal" in text section
    char **argv;   // reset command (as used by execv)
    tenv *env;     // extra vars to setenv()
} treset;

// expire item in expires (to expire items in avls of both files and journals)
typedef struct {
    re *re;        // pointer to re associated with hits
    keyhits *hits; // the hits we want to expire
} texpire;

typedef struct {
    char *pidfile;
    int logdst;
    char *logfile;
    int loglevel;
    char *logprefix;
} tglobal_cfg;

// defined in reactd_conf.y
extern tglobal_cfg cfg;
extern int parse_config(char *filename);

// these are defined in reactd.c
extern log_h *logh; // log handle
extern struct pollfd pollwatch; // used to store inotify file descriptor and to poll it for read events
extern int unwatchedfiles; // number of unwatched files (files in the config file but that are not being watched because they don't exist)

int keyhits_cmp(const void *a, const void *b, void *param);

#endif