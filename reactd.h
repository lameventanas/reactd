/*
 * $Id$
 */

#ifndef REACTD_H
#define REACTD_H
#define DEFAULT_CONFIG "/etc/reactd.conf"
#define DEFAULT_PIDFILE "/var/run/reactd/reactd.pid"
#define DEFAULT_STATEFILE "/var/run/reactd/reactd.db"

#define VERSION "0.1"
#define PROGRAM_NAME "reactd"
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define MAXFILES 64
#define MAXREACTIONS 64

#include <pcre.h>

#include <sys/inotify.h>
// for NAME_MAX:
#include <limits.h>

// for strchr:
#include <string.h>

// #define INOTIFY_EVENTS		(IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT)
#define INOTIFY_EVENTS		IN_MODIFY
#define INOTIFY_NEVENTS_NUM	4

#include <poll.h>

#include <assert.h>

#define MAX_RE_CAPTURES 10 // maximum number of RE captures

/* this keeps track of matches for reactions with a defined threshold, its an unsorted single linked list */
typedef struct {
	char *key;
	int first; // first timestamp occurrance (index to first in array)
	int last; // last timestamp occurrance (index to last in array)
	time_t *timestamp; // holds last threshold.count occurrances in a ring structure
	threshold_occurrance *next;
} threshold_occurrance;

typedef struct {
	char *name;
	int renum;
	struct {
		char *str;
		pcre *re;
		pcre_extra *re_studied;
		char *cmd;
		char *mail;
		struct {
			char *key;
			int count;
			int period;
			struct {
				int period;
				char *cmd;
			} reset;
			threshold_occurrance *occurrances; // this structure is not read from the config file
		} threshold;
	} reactions[MAXREACTIONS];
	off_t pos; // position read until now
	int watchfd; // unique watch descriptor associated with this file (as returned from inotify_add_watch)
} tfile;

tfile files[MAXFILES];

struct pollfd pollwatch; // used to store inotify file descriptor and to poll it for read events
int unwatchedfiles; // number of unwatched files (files in the config file but that are not being watched because they don't exist)

int filenum; // number of files we are watching

char *pidfile;
char *mail;
char *logging;
float version;

#ifdef DEBUG
#define dprintf printf
#else
#define dprintf
#endif
#endif