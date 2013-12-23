/*
 * %Id: $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
/*
 * $Id$
 */

#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#ifdef HAVE_INOTIFY_INIT
#include <sys/inotify.h>
#endif

#include "reactd.h"

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fprintf(out,
		_("\nUsage:\n"
		" %s [option] file\n"),
	 PROGRAM_NAME);
	
	fprintf(out, _(
		"\nOptions:\n"
		" -n, --lines NUMBER  output the last NUMBER lines\n"
		" -NUMBER             same as `-n NUMBER'\n"
		" -V, --version       output version information and exit\n"
		" -h, --help          display this help and exit\n\n"));
	
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv) {
	char *config = DEFAULT_CONFIG;
	int ch;
	struct stat st;
	
	static const struct option longopts[] = {
		{ "config",	required_argument,	0, 'c' },
		{ "version",	no_argument,		0, 'V' },
		{ "help",	no_argument,		0, 'h' },
		{ NULL,		0, 0, 0 }
	};
	
	while ((ch = getopt_long(argc, argv, "c:Vh", longopts, NULL)) != -1)
		switch((char)ch) {
			case 'c':
				config = optarg;
				break;
			case 'V':
				printf("%s version %s\n", PROGRAM_NAME, VERSION);
				exit(0);
			case 'h':
				usage(stdout);
			default:
				usage(stderr);
	}
	
	parseConfig(config);
	
	return EXIT_SUCCESS;
}


int parseConfig(char *configfile) {
	FILE *config;
	char line[MAXCONFIGLINE];
	char *p;
	
	config = fopen(configfile, "r");
	if (config == NULL) {
		fprintf(stderr, "Error opening %s: %s\n", configfile, strerror(errno));
		return 1;
	}
	
	while (fgets(line, MAXCONFIGLINE, config)) {
		p = line;
		while ((*p == ' ' || *p == '\t') && (p - line < strlen(line)))
			p++;
		
	}
	fclose(config);
	
	
}