/*
 * $Id$
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#ifdef HAVE_INOTIFY_INIT
#include <sys/inotify.h>
#endif

#include "reactd.h"

extern FILE *yyin;

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fprintf(out,
		"\nUsage:\n"
		" %s [option] file\n",
	PROGRAM_NAME);
	
	fprintf(out,
		"\nOptions:\n"
		" -c, --config FILE   configuration file (default: %s)\n"
		" -n, --lines NUMBER  output the last NUMBER lines\n"
		" -NUMBER             same as `-n NUMBER'\n"
		" -V, --version       output version information and exit\n"
		" -h, --help          display this help and exit\n\n", DEFAULT_CONFIG);
	
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
				PROGRAM_NAMEdefault:
				usage(stderr);
	}
	
	version = 0;
	pidfile = NULL;
	mail = NULL;
	logging = NULL;
	parseConfig(config);
	
	printf("\n---\nParsed configuration:\n\n");
	printf("version: %f\n", version);
	printf("pidfile: %f\n", pidfile);
	printf("logging: %f\n", logging);
	int i, j;
	for (i = 0; i < MAXFILES; i++) {
		printf("Checking filenr %d\n", i);
		if (files[i].filename == NULL)
			break;
		printf("* File: %s\n", files[i].filename);
		for (j = 0; j < MAXREACTIONS; j++) {
			printf("Checking renr %d\n", j);
			if (files[i].reactions[j].re_str == NULL)
				break;
			printf("    * RE: %s\n", files[i].reactions[j].re_str);
			printf("    * cmd: %s\n", files[i].reactions[j].cmd);
			printf("    * mail: %s\n", files[i].reactions[j].mail);
			printf("    * threshold key: %s\n", files[i].reactions[j].threshold.key);
			printf("    * threshold count: %d\n", files[i].reactions[j].threshold.count);
			printf("    * threshold period: %d\n", files[i].reactions[j].threshold.period);
			printf("    * threshold reset period: %d\n", files[i].reactions[j].threshold.reset.period);
			printf("    * threshold reset cmd: %d\n", files[i].reactions[j].threshold.reset.cmd);
		}
	}
	
	return EXIT_SUCCESS;
}


int parseConfig(char *configfile) {
	yyin = fopen(configfile, "r");
	if (yyin == NULL) {
		fprintf(stderr, "Error opening %s: %s\n", configfile, strerror(errno));
		return 1;
	}
	do {
		yyparse();
	} while (!feof(yyin));
	fclose(yyin);
	return 0;
}
