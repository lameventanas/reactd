/*
 * $Id$
 */

#ifndef REACTD_H
#define REACTD_H
#define DEFAULT_CONFIG "/etc/reactd.conf"

#define VERSION "0.1"
#define PROGRAM_NAME "reactd"
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define MAXFILES 64
#define MAXREACTIONS 64

#include <pcre.h>

typedef struct {
	int period;
	char *cmd;
} treset;

typedef struct {
	char *key;
	int count;
	int period;
	treset *reset;
} tthreshold;

struct {
	char *filename;
	struct {
		char *re_str;
		pcre *re;
		char *cmd;
		char *mail;
		tthreshold *threshold;
	} reactions[MAXREACTIONS];
} files[MAXFILES];

#endif