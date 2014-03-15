/*
 * $Id$
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

#include "reactd.h"
#include "threshold.h"

extern FILE *yyin;

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fprintf(out,
		"\nUsage:\n"
		" %s [option] file\n",
	PROGRAM_NAME);
	
	fprintf(out,
		"\nOptions:\n"
		" -c, --config FILE      configuration file (default: %s)\n"
		" -p, --pidfile FILE     pid file (default: %s)\n"
		" -s, --statefile FILE   state file (default: %s)\n"
		" -V, --version          output version information and exit\n"
		" -h, --help             display this help and exit\n\n", DEFAULT_CONFIG, DEFAULT_PIDFILE, DEFAULT_STATEFILE);
	
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int parseConfig(char *configfile) {
	int ret;
/*	filenum = 0;
	version = 0;
	pidfile = NULL;
	mail = NULL;
	logging = NULL;
*/	
	int i;
	
	dprintf("Initializing %d bytes in 0x%x\n", sizeof(files), files);
	memset(files, 0, sizeof(files));
	
	yyin = fopen(configfile, "r");
	if (yyin == NULL) {
		fprintf(stderr, "Error opening %s: %s\n", configfile, strerror(errno));
		return 1;
	}
	ret = yyparse();
	fclose(yyin);
	
	return ret;
}


void react(tfile *file) {
	struct stat st;
	int fd;
	char buf[8192];
	int r;
	char *end;
	
	dprintf("Reacting on file %s\n", file->name);
	// check if the file was truncated
	if (-1 == stat(file->name, &st))
		return;
	if (st.st_size < file->pos) {
		dprintf("File %s shrunk, reseting\n", file->name);
		file->pos = 0;
	}
	
	fd = open(file->name, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Error opening %s: %s\n", file->name, strerror(errno));
		return;
	}
	
	do {
		dprintf("Seeking %s to position %d\n", file->name, file->pos);
		if (-1 == lseek(fd, file->pos, SEEK_SET)) {
			fprintf(stderr, "Error seeking %s to position %d: %s\n", file->name, file->pos, strerror(errno));
			close(fd);
		}
		r = read(fd, buf, sizeof(buf));
		dprintf("Read %d bytes\n", r);
		
		if (r == -1) {
			fprintf(stderr, "Error reading from %s: %s\n", file->name, strerror(errno));
			close(fd);
			return;
		}
		if (r > 0) {
			end = strchr(buf, 0xa);
			if (end == NULL) {
				if (r == sizeof(buf)) { // if filled the buffer but there is no newline, process the whole buffer as if it is a complete line
					dprintf("Buffer is full but no newline found, taking whole buffer\n");
					end = &buf[sizeof(buf) - 1];
					file->pos--; // re-read the last byte (because we will overwrite it in buf)
				} /*
				else { // read a partial line, ignore until another write adds a \n
					dprintf("Partial string: '");
					int z;
					for (z=0; z<r; z++)
						printf("%c", buf[z]);
					printf("'\n");
					close(fd);
					return;
				} */
			}
			*end = 0; // null terminate string
			file->pos += strlen(buf) + 1; // set next position to read from this file
			dprintf(" ---> '%s' (%d bytes)\n", buf, strlen(buf));

			// match against all possible REs for this file
			int i;
			int re_ret[3 * MAX_RE_CAPTURES];
			int matches;
			for (i = 0; i < file->renum; i++) {
				matches = pcre_exec(file->re[i].re, file->re[i].re_studied, buf, strlen(buf), 0, 0, re_ret, 3*MAX_RE_CAPTURES);
				
				// NOTE: 0 means return vector overflow, and negative numbers are errors
				if (matches >= 0) {
					char *key;
					dprintf("! String '%s' matched re '%s'\n", buf, file->re[i].str);
					
					// replace the captured strings in the KEY from config, then record occurrance
					
					/*
					// record occurrance
					threshold_record_occurrance(&file->re[i].threshold, key);
					// check if event has been triggered
					occurrances = keylist_get(&file->re[i].threshold->occurrances, key);
					*/
					
					dprintf("! Running cmd: %s\n", files->re[i].cmd);
				}
			}
			
		}
	} while (r > 0);
	close(fd);
}

int main(int argc, char **argv) {
	char *config = DEFAULT_CONFIG;
	int ch;
	
	static const struct option longopts[] = {
		{ "config",	required_argument,	0, 'c' },
		{ "pidfile",	required_argument,	0, 'p' },
		{ "statefile",	required_argument,	0, 's' },
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
	
	if (0 != parseConfig(config)) {
		printf("Error parsing %s\n", config);
		return 1;
	}
	
	// pcre vars
	const unsigned char *pcre_tables = pcre_maketables();
	const char *error_msg;
	int error_off;
#define PCRE_OPTIONS 0
	
	dprintf("Global options:\nversion: %f\npidfile: %s\nlogging: %s\n", version, pidfile, logging);
	int i, j;
	for (i = 0; i < filenum; i++) {
		dprintf(" * File %d: %s\n", i, files[i].name);
		
		for (j = 0; j < files[i].renum; j++) {
			files[i].re[j].re = pcre_compile(files[i].re[j].str, PCRE_OPTIONS, &error_msg, &error_off, pcre_tables);
			
			if (! files[i].re[j].re) {
				fprintf(stderr, "Error in regular expression '%s' at %d: %s\n", files[i].re[j].str, error_off, error_msg);
				return 1;
			}
			
			files[i].re[j].re_studied = pcre_study(files[i].re[j].re, 0, &error_msg);
			
			dprintf("    * re %d: %s\n", j, files[i].re[j].str);
			dprintf("       * cmd: %s\n", files[i].re[j].cmd);
			dprintf("       * mail: %s\n", files[i].re[j].mail);
			dprintf("       * threshold key: %s\n", files[i].re[j].threshold.config.key);
			dprintf("       * threshold trigger count: %d\n", files[i].re[j].threshold.config.trigger_count);
			dprintf("       * threshold trigger period: %d\n", files[i].re[j].threshold.config.trigger_period);
			dprintf("       * threshold reset count: %d\n", files[i].re[j].threshold.config.reset_count);
			dprintf("       * threshold reset period: %d\n", files[i].re[j].threshold.config.reset_period);
			dprintf("       * threshold reset cmd: %d\n", files[i].re[j].threshold.config.reset_cmd);
		}
	}

	// Initialize global inotify fd and poll structure
	pollwatch.fd = inotify_init();
	pollwatch.events = POLLIN;
	unwatchedfiles = filenum; // number of files not currently watched
	
	if (pollwatch.fd == -1) {
		fprintf(stderr, "Error in inotify_init(): %s\n", strerror(errno));
		exit(1);
	}
	
	// Initialize watchfd for each file
	for (i = 0; i < filenum; i++) {
		files[i].watchfd = inotify_add_watch(pollwatch.fd, files[i].name, INOTIFY_EVENTS);
		if (files[i].watchfd != -1) {
			struct stat st;
			if (-1 == stat(files[i].name, &st)) {
				// if stat fails, unmonitor the file
				files[i].watchfd = -1;
				inotify_rm_watch(pollwatch.fd, files[i].watchfd);
				files[i].watchfd = -1;
			} else {
				files[i].pos = st.st_size;
				unwatchedfiles--;
			}
		}
	}
	
	char inotify_buf[ INOTIFY_EVENTS * (sizeof(struct inotify_event) + NAME_MAX + 1)];
	ssize_t len;
	int e;
	
	int pollret;
	int polltimeout;

	while (1) {
		polltimeout = unwatchedfiles > 0 ? 5000 : -1;
		dprintf("%d files configured (%d unmonitored) timeout: %d\n", filenum, unwatchedfiles, polltimeout);
		pollret = poll(&pollwatch, 1, polltimeout);
		
		if (pollret < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else {
				fprintf(stderr, "Error in poll: %s\n", strerror(errno));
				exit(1);
			}
		}
		
		if (pollret == 0) { // timeout: check unmonitored files
			// dprintf("Checking unwatched files\n");
			int i;
			for (i = 0; i < filenum && unwatchedfiles > 0; i++) {
				if (files[i].watchfd == -1) {
					// dprintf("Found unwatched file: %s\n", files[i].name);
					files[i].watchfd = inotify_add_watch(pollwatch.fd, files[i].name, INOTIFY_EVENTS);
					if (files[i].watchfd != -1) {
						files[i].pos = 0;
						dprintf("Now monitoring %s\n", files[i].name);
						unwatchedfiles--;
					}
				}
			}
			continue; // back to polling
		}
		
		// read inotify event(s)
		len = read(pollwatch.fd, inotify_buf, sizeof(inotify_buf));
		if (len < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else {
				fprintf(stderr, "Error reading inotify event: %s\n", strerror(errno));
				exit(1);
			}
		}
		// process each inotify event
		for (e = 0; e < len; ) {
			struct inotify_event *ev = (struct inotify_event *) &inotify_buf[e];
			tfile *file = NULL;
			
			// first find affected file
			int i;
			for (i = 0; i < filenum; i++) {
				if (files[i].watchfd == ev->wd) {
					file = &files[i];
					dprintf("Affected file: %s\n", file->name);
					break;
				}
			}
			assert(file != NULL); // could not find file associated with inotify event!
			if (ev->mask & IN_MODIFY)  {
				react(file);
				
			} else if (ev->mask & IN_IGNORED)  {
				file->watchfd = -1;
				dprintf("File %s is now unwatched\n", file->name);
				unwatchedfiles++;
			}
			
			
			// move pointer to next event in buffer
			e += sizeof(struct inotify_event) + ev->len;
		}
	}
	
	
	return EXIT_SUCCESS;
}
