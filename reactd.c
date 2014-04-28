/*
 * $Id$
 */
#include "debug.h"

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

#include "log.h"
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
	loglevel = LOG_INFO;
*/	
	int i;
	
	dprint("Initializing %d bytes in 0x%x", sizeof(files), files);
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
	
	logw(logh, LOG_DEBUG, "Reacting on file %s", file->name);
	// check if the file was truncated
	if (-1 == stat(file->name, &st))
		return;
	if (st.st_size < file->pos) {
		logw(logh, LOG_DEBUG, "File %s shrunk, reseting", file->name);
		file->pos = 0;
	}
	
	fd = open(file->name, O_RDONLY);
	if (fd == -1) {
		logw(logh, LOG_ERR, "Error opening %s: %s", file->name, strerror(errno));
		return;
	}
	
	do {
		logw(logh, LOG_DEBUG, "Seeking %s to position %d", file->name, file->pos);
		if (-1 == lseek(fd, file->pos, SEEK_SET)) {
			logw(logh, LOG_ERR, "Error seeking %s to position %d: %s", file->name, file->pos, strerror(errno));
			close(fd);
		}
		r = read(fd, buf, sizeof(buf));
		logw(logh, LOG_DEBUG, "Read %d bytes", r);
		
		if (r == -1) {
			logw(logh, LOG_ERR, "Error reading from %s: %s", file->name, strerror(errno));
			close(fd);
			return;
		}
		if (r > 0) {
			end = strchr(buf, 0xa);
			if (end == NULL) {
				if (r == sizeof(buf)) { // if filled the buffer but there is no newline, process the whole buffer as if it is a complete line
					logw(logh, LOG_DEBUG, "Buffer is full but no newline found, taking whole buffer");
					end = &buf[sizeof(buf) - 1];
					file->pos--; // re-read the last byte (because we will overwrite it in buf)
				} /*
				else { // read a partial line, ignore until another write adds a \n
					dprint("Partial string: '");
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
			logw(logh, LOG_DEBUG, " -->%s<-- (%d bytes)", buf, strlen(buf));

			// match against all configured REs for this file
			int i;
			for (i = 0; i < file->renum; i++) {
				int buf_ret[3 * MAX_RE_CAPTURES];
				int buf_matches;
				buf_matches = pcre_exec(file->re[i].re, file->re[i].re_studied, buf, strlen(buf), 0, 0, buf_ret, 3*MAX_RE_CAPTURES);
				
				// NOTE: 0 means return vector overflow, and negative numbers are errors
				if (buf_matches >= 0) {
					int run = 1;
					// dprint("! String '%s' matched re '%s' matches: %d", buf, file->re[i].str, matches);
					
					// check first if there is a threshold configured for this RE
					if (file->re[i].threshold.config.trigger_count > 0 ) {
						char *key;
						// if there is a threshold for this RE, record occurrance and maybe run command
						key = pcre_subst_replace(buf, file->re[i].threshold.config.re_subst_key, buf_ret, 3*MAX_RE_CAPTURES, buf_matches, PCRE_SUBST_DEFAULT);
					
						run = threshold_record_occurrance(&file->re[i].threshold, key);
						logw(logh, LOG_DEBUG, "Recorded occurrance of %s, run: %d", key, run);
					
						free(key);
					}
					// replace the captured strings in the KEY from config, then record occurrance
					
					/*
					// record occurrance
					threshold_record_occurrance(&file->re[i].threshold, key);
					// check if event has been triggered
					occurrances = keylist_get(&file->re[i].threshold->occurrances, key);
					*/
					if (run) {
						char *cmd = pcre_subst_replace(buf, file->re[i].cmd_subst, buf_ret, 3*MAX_RE_CAPTURES, buf_matches, PCRE_SUBST_SHELL_ESCAPE_SUBJ);
						
						logw(logh, LOG_DEBUG, "*** Running cmd: %s", cmd);
						system(cmd);
						free(cmd);
					}
				}
			}
			
		}
	} while (r > 0);
	close(fd);
}

void update_all_thresholds() {
	int i;
	int j;
	keylist *thkey;
	
	logw(logh, LOG_DEBUG, "update_all_thresholds");
	for (i = 0; i < filenum; i++) {
		for (j = 0; j < files[i].renum; j++) {
			// if there is a reset for this threshold
			if (files[i].re[j].threshold.config.trigger_period > 0) {
				logw(logh, LOG_DEBUG, "Checking reset of threshold for file %d re %d", i, j);
				for (thkey = files[i].re[j].threshold.occurrances; thkey != NULL; thkey = thkey->next) {
					logw(logh, LOG_DEBUG, "Checking key %s", thkey->key);
					int run = threshold_update_status(&files[i].re[j].threshold, thkey->key);
					logw(logh, LOG_DEBUG, "Running reset command for key %s: %d", thkey->key, run);
					if (run) {
						// the key will be in an environment variable (maybe I should do some replacement thing)
						logw(logh, LOG_DEBUG, " *** Running reset command for key %s: %s", thkey->key, files[i].re[j].threshold.config.reset_cmd);
						system(files[i].re[j].threshold.config.reset_cmd);
					}
				}
			}
		}
	}
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
	
	logh = log_open(logtype_str(logging, LOG_TO_SYSLOG), loglevel_str(loglevel, LOG_INFO), logprefix, logfile);
	logw(logh, LOG_INFO, "Starting reactd");
	
	
	// pcre vars
	const unsigned char *pcre_tables = pcre_maketables();
	const char *error_msg;
	int error_off;
#define PCRE_OPTIONS 0
	
	dprint_init();
	dprint("Global options:\nversion: %f\npidfile: %s\nlogging: %s\nloglevel: %d\n", version, pidfile, logging, loglevel);
	int i, j;
	for (i = 0; i < filenum; i++) {
		logw(logh, LOG_DEBUG, " * File %d: %s", i, files[i].name);
		
		for (j = 0; j < files[i].renum; j++) {
			files[i].re[j].re = pcre_compile(files[i].re[j].str, PCRE_OPTIONS, &error_msg, &error_off, pcre_tables);
			
			if (! files[i].re[j].re) {
				logw(logh, LOG_ERR, "Error in regular expression '%s' at %d: %s\n", files[i].re[j].str, error_off, error_msg);
				return 1;
			}
			
			files[i].re[j].re_studied = pcre_study(files[i].re[j].re, 0, &error_msg);
			files[i].re[j].cmd_subst = pcre_subst_study(files[i].re[j].cmd, PCRE_SUBST_DEFAULT);
			
			// if a threshold has been configured, study the key replacement string to make substitutions on every match
			if (files[i].re[j].threshold.config.trigger_count > 0 && files[i].re[j].threshold.config.trigger_period > 0) {
				files[i].re[j].threshold.config.re_subst_key = pcre_subst_study(files[i].re[j].threshold.config.key, PCRE_SUBST_DEFAULT);
			}
				
			logw(logh, LOG_DEBUG, "    * re %d: %s", j, files[i].re[j].str);
			logw(logh, LOG_DEBUG, "       * cmd: %s", files[i].re[j].cmd);
			logw(logh, LOG_DEBUG, "       * mail: %s", files[i].re[j].mail);
			logw(logh, LOG_DEBUG, "       * threshold key: %s", files[i].re[j].threshold.config.key);
			logw(logh, LOG_DEBUG, "       * threshold trigger count: %d", files[i].re[j].threshold.config.trigger_count);
			logw(logh, LOG_DEBUG, "       * threshold trigger period: %d", files[i].re[j].threshold.config.trigger_period);
			logw(logh, LOG_DEBUG, "       * threshold reset count: %d", files[i].re[j].threshold.config.reset_count);
			logw(logh, LOG_DEBUG, "       * threshold reset period: %d", files[i].re[j].threshold.config.reset_period);
			logw(logh, LOG_DEBUG, "       * threshold reset cmd: %d", files[i].re[j].threshold.config.reset_cmd);
			if (files[i].re[j].threshold.config.trigger_count > 0) {
				logw(logh, LOG_DEBUG, "       * threshold subst key data: 0x%X", files[i].re[j].threshold.config.re_subst_key);
				// pcre_subst_print(files[i].re[j].threshold.config.re_subst_key);
			}
		}
	}

	// Initialize global inotify fd and poll structure
	pollwatch.fd = inotify_init();
	pollwatch.events = POLLIN;
	unwatchedfiles = filenum; // number of files not currently watched
	
	if (pollwatch.fd == -1) {
		logw(logh, LOG_ERR, "Error in inotify_init(): %s", strerror(errno));
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

	// TODO: should monitor for non-existant (unmonitored) files by watching their directory via inotify
	// that way the poll timeout will only be used to reset thresholds, and also I won't miss the first writes of an unmonitored file (until the first timeout passes), but this is hard, and also, the directory itself could be deleted and created again, then nothing will be monitored anymore inside it
	while (1) {
		polltimeout = unwatchedfiles > 0 ? 5000 : -1;
		logw(logh, LOG_DEBUG, "%d files configured (%d unmonitored) timeout: %d", filenum, unwatchedfiles, polltimeout);
		pollret = poll(&pollwatch, 1, polltimeout);
		
		if (pollret < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else {
				logw(logh, LOG_ERR, "Error in poll: %s", strerror(errno));
				exit(1);
			}
		}
		
		if (pollret == 0) { // timeout: check unmonitored files
			// dprint("Checking unwatched files");
			int i;
			for (i = 0; i < filenum && unwatchedfiles > 0; i++) {
				if (files[i].watchfd == -1) {
					// logw(logh, LOG_DEBUG, "Found unwatched file: %s", files[i].name);
					files[i].watchfd = inotify_add_watch(pollwatch.fd, files[i].name, INOTIFY_EVENTS);
					if (files[i].watchfd != -1) {
						files[i].pos = 0;
						logw(logh, LOG_DEBUG, "Now monitoring %s", files[i].name);
						unwatchedfiles--;
					}
				}
			}
			
			// TODO: here we should only update the thresholds every minute, not every timeout
			update_all_thresholds();
			
			continue; // back to polling
		}
		
		// read inotify event(s)
		len = read(pollwatch.fd, inotify_buf, sizeof(inotify_buf));
		if (len < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else {
				logw(logh, LOG_ERR, "Error reading inotify event: %s", strerror(errno));
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
					logw(logh, LOG_DEBUG, "Affected file: %s", file->name);
					break;
				}
			}
			assert(file != NULL); // could not find file associated with inotify event!
			if (ev->mask & IN_MODIFY)  {
				react(file);
				
			} else if (ev->mask & IN_IGNORED)  {
				file->watchfd = -1;
				logw(logh, LOG_DEBUG, "File %s is now unwatched", file->name);
				unwatchedfiles++;
			}
			
			
			// move pointer to next event in buffer
			e += sizeof(struct inotify_event) + ev->len;
		}
	}
	
	log_close(logh);
	
	return EXIT_SUCCESS;
}
