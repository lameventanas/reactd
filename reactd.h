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

#define MAXCONFIGLINE 1024
#define MAXFILES 16
#define MAXREACTIONS 32

#include <pcre.h>

typedef struct {
	pcre *re;
	char *cmd;
	char *args;
} treaction;

typedef struct {
	char *filename;
	treaction reactions[MAXREACTIONS];
} tmonitor_file;

#endif