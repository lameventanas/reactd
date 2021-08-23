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
#include <signal.h>
#include <time.h>
#include <math.h>

#include "reset_list.h"
#include "log.h"
#include "reactd.h"

extern tfile *tfs;

unsigned char *pcre_tables = NULL;

log_h *logh; // log handle
treset_list *reset_list = NULL;
int timeout = -1; // poll timeout for all files, -1 if all monitored files exist, otherwise LOG_CREATE_SCAN_INTERVAL

static volatile sig_atomic_t exit_flag = 0;

struct pollfd pollwatch; // used to store inotify file descriptor and to poll it for read events

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "\nUsage:\nreactd [option] file\n");

    fprintf(out,
        "\nOptions:\n"
        " -c, --config FILE      configuration file (default: %s)\n"
        " -p, --pidfile FILE     pid file (default: %s)\n"
        " -s, --statefile FILE   state file (default: %s)\n"
        " -V, --version          output version information and exit\n"
        " -h, --help             display this help and exit\n\n", DEFAULT_CONFIG, DEFAULT_PIDFILE, DEFAULT_STATEFILE);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int run_prog(char *prog) {
    pid_t pid = fork();
    if (pid == 0) {
        if (daemon(0, 0) < 0) {
            syslog(LOG_ERR, "daemon: %m");
            exit(1);
        }
        execl(prog, prog, NULL);
        syslog(LOG_ERR, "Could not run %s: %m", prog);
        exit(1);
    }
    if (pid < 0) {
        syslog(LOG_ERR, "Could not fork: %m");
        return 0;
    }
    return 1;
}

// run reset for a specific key
void reset_run(char *key, char *cmd) {
    syslog(LOG_DEBUG, "Resetting item with key %s with cmd %s", key, cmd);
    setenv("REACT_KEY", key, 1);
    run_prog(cmd);
    unsetenv("REACT_KEY");
    free(key);
}

// check if any items must be reset
// returns timeout until next check should run
int reset_check() {
    syslog(LOG_DEBUG, "reset_check");
    time_t t = time(NULL);
    reset_list_run(reset_list, t, reset_run);
    time_t ret = reset_list_next_reset(reset_list);
    syslog(LOG_DEBUG, "next_reset: %d", ret);
    return ret < 0 ? ret : 1000 * (ret - t) + RESET_GUARD_TIME;
}

void reset_item_free(time_t t, char *key, char *cmd) {
    printf("freeing reset_item %p: %s\n", key, key);
    free(key);
}

void free_hits(void *ring) {
    ring_free(ring, free);
}
void free_config() {
    if (cfg.pidfile)
        free(cfg.pidfile);
    if (cfg.logging)
        free(cfg.logging);
    if (cfg.logfile)
        free(cfg.logfile);
    if (cfg.logprefix)
        free(cfg.logprefix);
    if (cfg.loglevel)
        free(cfg.loglevel);

    for (tfile *tf = tfs; tf->name; tf++) {
        printf("freeing TF %s %p\n", tf->name, tf->name);
        free(tf->name);
        for (re *re = tf->re; re->str; re++) {
            printf("    freeing RE %s %p\n", re->str, re->str);
            free(re->str);
            if (re->re)
                pcre_free(re->re);
            if (re->cmd)
                free(re->cmd);
            if (re->reset_cmd)
                free(re->reset_cmd);
            if (re->re_studied)
                pcre_free_study(re->re_studied);
            if (re->key)
                pcre_subst_free(re->key);
            if (re->hits)
                keylist_free(&re->hits, free_hits);
        }
        free(tf->re);
    }
    free(tfs);
}

// note: this only works for x>=0
#define num_digits(x) (unsigned int)(x==0?1:(1+log10(x)))

// callback for tail_lines
// runs record_hit function
void proc_line(tfile *tf, char *s) {
    for (re *re = tf->re; re->str; re++) {
        int capture_cnt; // used to determine number of capture groups
        pcre_fullinfo(re->re, re->re_studied, PCRE_INFO_CAPTURECOUNT, &capture_cnt);
        capture_cnt++; // add space for \0
        int *matches = malloc(3 * capture_cnt * sizeof(int));

        syslog(LOG_DEBUG, "matching re %s against %s", re->str, s);
        int match_cnt = pcre_exec(re->re, re->re_studied, s, strlen(s), 0, 0, matches, 3 * capture_cnt);
        if (match_cnt > 0) {
            syslog(LOG_INFO, "%s matched RE %s count=%d", tf->name, re->str, match_cnt);

            if (re->key != NULL && re->trigger_cnt > 0) {
                char *key = pcre_subst_replace(s, re->key, matches, 3*MAX_RE_CAPTURES, match_cnt, 0);
                syslog(LOG_DEBUG, "got key: %s", key);
                ring *hits = keylist_get(&re->hits, key);
                if (hits == NULL) {
                    syslog(LOG_DEBUG, "creating ring");
                    hits = ring_init(re->trigger_cnt);
                    keylist_set(&re->hits, key, hits);
                }
                time_t *hit_time = malloc(sizeof(time_t));
                time(hit_time);
                time_t *old_time = ring_put(hits, (void *)hit_time);
                if (old_time != NULL)
                    free(old_time);
                if (ring_count(hits) == re->trigger_cnt) {
                    time_t *hit_first = ring_get_oldest(hits, 0);
                    if (*hit_time - *hit_first <= re->trigger_time) {
                        syslog(LOG_NOTICE, "%s RE %s triggered by %u hits in %u seconds", tf->name, re->str, ring_get_size(hits), *hit_time - *hit_first);

                        setenv("REACT_KEY", key, 1);
                        setenv("REACT_FILE", tf->name, 1);

                        for (unsigned int i = 0; i < match_cnt; i++) {
                            char *v = strndup(&s[matches[2*i]], matches[2*i+1] - matches[2*i]);
                            char k[7 + num_digits(i)];
                            sprintf(k, "REACT_%u", i);
                            setenv(k, v, 1);
                            free(v);
                        }
                        if (run_prog(re->cmd)) {
                            // TODO: add to unban fifo
                            syslog(LOG_DEBUG, "Banned key %s for %u seconds", key, re->reset_time);
                            if (re->reset_cmd != NULL && re->reset_time > 0) {
                                // NOTE: reset_list_run callback should free key but not reset_cmd
                                reset_list_add(reset_list, *hit_time + re->reset_time, strdup(key), re->reset_cmd);
                                if (timeout < 0 || timeout > 1000 * re->reset_time + RESET_GUARD_TIME) {
                                    syslog(LOG_DEBUG, "Setting timeout to %u", re->reset_time);
                                    timeout = 1000 * re->reset_time + RESET_GUARD_TIME;
                                }
                                syslog(LOG_DEBUG, "After ban timeout = %u", timeout);
                            }
                        }
                        unsetenv("REACT_FILE");
                        unsetenv("REACT_KEY");
                        for (unsigned int i = 0; i < match_cnt; i++) {
                            char k[8];
                            snprintf(k, 8, "REACT_%u", i);
                            unsetenv(k);
                        }
                    }
                }
                free(key);
            }
        }
        free(matches);
    }
}

void tail_lines(tfile *tf, void (*callback)(tfile *, char *)) {
    struct stat st;
    int fd;

    usleep(EVENT_REACT_TIME);
    if (-1 == stat(tf->name, &st)) {
        syslog(LOG_ERR, "stat: %s: %m", tf->name);
        return;
    }

    syslog(LOG_DEBUG, "tail pos: %d size: %d", tf->pos, st.st_size);

    if (st.st_size == 0) {
        syslog(LOG_WARNING, "File reset: %s", tf->name);
        tf->pos = 0;
        return;
    }
    if (st.st_size < tf->pos) {
        syslog(LOG_WARNING, "File shrunk, will reset: %s", tf->name);
        tf->pos = 0;
    }
    if (st.st_size == tf->pos) {
        syslog(LOG_DEBUG, "Size didn't change: %s", tf->name);
        return;
    }

    fd = open(tf->name, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        syslog(LOG_ERR, "open: %s: %m", tf->name);
        return;
    }

#ifdef USE_MMAP
    syslog(LOG_DEBUG, "using mmap");

    ssize_t len = st.st_size - tf->pos;
    off_t tf_off = tf->pos % sysconf(_SC_PAGE_SIZE);
    char *addr = mmap(NULL, len + tf_off, PROT_READ, MAP_SHARED, fd, tf->pos - tf_off);
    if (addr == MAP_FAILED) {
        syslog(LOG_ERR, "mmap: %m");
        close(fd);
        return;
    }

    ssize_t r = 0;
    while (r < len && !exit_flag) {
        off_t pos = 0;
        do {
            if (*(addr + tf_off + pos) == 0)
                break;
            pos++;
        } while (pos < len && *(addr + tf_off + pos) != '\n');

        if (*(addr + tf_off + pos) == '\n') {
            char *line = strndup(addr + tf_off, pos);
            syslog(LOG_DEBUG, line);
            (*callback)(tf, line);
            free(line);
        }
        r += pos + 1;
        tf_off += pos + 1;
    }

    tf->pos += r;
    munmap(addr, len);

#else

    if (tf->pos > 0)
        if (-1 == lseek(fd, tf->pos, SEEK_SET)) {
            perror("lseek");
            close(fd);
            return;
        }
    ssize_t sz = 0;
    char c;
    while (tf->pos + sz < st.st_size) {
        if (-1 == read(fd, &c, 1)) {
            perror("read");
            close(fd);
            return;
        }
        sz++;
        if (c == '\n') {
            syslog(LOG_DEBUG, "found newline at pos=%d sz=%d", tf->pos, sz);
            if (-1 == lseek(fd, tf->pos, SEEK_SET)) {
                perror("lseek");
                close(fd);
                return;
            }
            char *line = malloc(sz);
            if (NULL == line) {
                perror("malloc");
                close(fd);
                return;
            }

            if (sz != read(fd, line, sz)) {
                perror("read");
                close(fd);
                return;
            }
            line[sz-1] = 0;
            syslog(LOG_DEBUG, line);
            (*callback)(tf, line);
            free(line);
            tf->pos += sz;
            sz = 0;
        }
    }

#endif

    close(fd);
    syslog(LOG_DEBUG, "new tail pos: %d size: %d", tf->pos, st.st_size);
}

void prog_exit(int sig) {
    syslog(LOG_NOTICE, "Received signal %d, exiting", sig);
    exit_flag = 1;
}

int main(int argc, char **argv) {
    char *config = DEFAULT_CONFIG;
    int ch;

    static const struct option longopts[] = {
        { "config",    required_argument,    0, 'c' },
        { "pidfile",    required_argument,    0, 'p' },
        { "statefile",    required_argument,    0, 's' },
        { "version",    no_argument,        0, 'V' },
        { "help",    no_argument,        0, 'h' },
        { NULL,        0, 0, 0 }
    };

    while ((ch = getopt_long(argc, argv, "c:Vh", longopts, NULL)) != -1)
        switch((char)ch) {
            case 'c':
                config = optarg;
                break;
            case 'V':
                printf("reactd version %s\n", VERSION);
                exit(0);
            case 'h':
                usage(stdout);
            default:
                usage(stderr);
    }

    memset(&cfg, 0, sizeof(cfg));
    if (0 != parse_config(config)) {
        printf("Error parsing %s\n", config);
        return 1;
    }

    logh = log_open(logtype_str(cfg.logging, LOG_TO_SYSLOG), loglevel_str(cfg.loglevel, LOG_INFO), cfg.logprefix, cfg.logfile);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, prog_exit);
    signal(SIGTERM, prog_exit);

    logw(logh, LOG_INFO, "Starting reactd");

    // pcre vars
    pcre_tables = (unsigned char *)pcre_maketables();
    printf("pcre_tables: %p\n", pcre_tables);
    const char *error_msg;
    int error_off;

    dprint_init();
    dprint("Global options:\nversion: %u.%u\npidfile: %s\nlogging: %s\nloglevel: %s\n", cfg.version_major, cfg.version_minor, cfg.pidfile, cfg.logging, cfg.loglevel);

    // Initialize global inotify fd and poll structure
    pollwatch.fd = inotify_init();
    pollwatch.events = POLLIN;

    if (pollwatch.fd == -1) {
        logw(logh, LOG_ERR, "Error in inotify_init(): %s", strerror(errno));
        exit(1);
    }

    reset_list = reset_list_init();
    struct stat st;
    unsigned int tf_cnt_exist = 0; // number of monitored files that exist
    unsigned int tf_cnt_total = 0; // number of monitored files

    for (tfile *tf = tfs; tf->name; tf++) {
        tf_cnt_total++;
        tf->watchfd = inotify_add_watch(pollwatch.fd, tf->name, IN_MODIFY|IN_IGNORED);
        if (-1 == stat(tf->name, &st)) {
            // file to monitor doesn't exist
            timeout = LOG_CREATE_SCAN_INTERVAL;
            syslog(LOG_DEBUG, "%s doesn't exist, new timeout: %d", tf->name, timeout);
            tf->pos = 0;
        } else {
            tf_cnt_exist++;
            tf->pos = st.st_size;
            syslog(LOG_DEBUG, "%s position: %u", tf->name, tf->pos);
        }

        // for (int i = 0; tf->re[i].str; i++) printf("    re: %p %s\n", tf->re[i], tf->re[i].str);
    }

    char inotify_buf[ 100 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    while (!exit_flag) {
        syslog(LOG_DEBUG, "Monitors: %u/%u sleeping %d milliseconds", tf_cnt_exist, tf_cnt_total, timeout);
        int pollret = poll(&pollwatch, 1, timeout);

        // error
        if (pollret < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                syslog(LOG_DEBUG, "poll interrupted by signal");
                if (!exit_flag) {
                    timeout = reset_check();
                    if (tf_cnt_exist < tf_cnt_total && LOG_CREATE_SCAN_INTERVAL < timeout)
                        timeout = LOG_CREATE_SCAN_INTERVAL;
                }
                continue;
            } else {
                syslog(LOG_ERR, "poll: %m");
                exit(1);
            }
        }

        // timeout (host is ready to be unbanned or timed out waiting for log file to be created)
        if (pollret == 0) {
            timeout = reset_check();
            if (tf_cnt_exist < tf_cnt_total) {
                // check if file has been created
                for (tfile *tf = tfs; tf->name; tf++) {
                    if (tf->watchfd == -1) {
                        tf->watchfd = inotify_add_watch(pollwatch.fd, tf->name, IN_MODIFY|IN_IGNORED);
                        if (tf->watchfd == -1) {
                            syslog(LOG_DEBUG, "%s not monitored: %m", tf->name);
                        } else {
                            tf_cnt_exist++;
                            syslog(LOG_DEBUG, "%s monitoring from now", tf->name);
                            tf->pos = 0;
                            tail_lines(tf, proc_line);
                        }
                    }
                }
                if (tf_cnt_exist < tf_cnt_total && (timeout < 0 || timeout > LOG_CREATE_SCAN_INTERVAL ) )
                    timeout = LOG_CREATE_SCAN_INTERVAL;
            }
            continue;
        }

        // inotify event arrived
        ssize_t len = read(pollwatch.fd, inotify_buf, sizeof(inotify_buf));
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else {
                syslog(LOG_ERR, "Error reading inotify event: %m");
                exit(1);
            }
        }
        for (int e = 0; e < len; ) {
            struct inotify_event *ev = (struct inotify_event *) &inotify_buf[e];
            syslog(LOG_DEBUG, "Inotify event pos %d/%d: %#08x", e, len, ev->mask);

            // find out which monitored file received the event
            tfile *tf;
            for (tf = tfs; tf->name; tf++)
                if (tf->watchfd == ev->wd)
                    break;
            assert(tf->name != NULL);
            syslog(LOG_DEBUG, "%s got event", tf->name);

            if (ev->mask & IN_MODIFY) {
                tail_lines(tf, proc_line);
                // timeout = reset_check();
            }
            if (ev->mask & IN_IGNORED) {
                syslog(LOG_DEBUG, "%s deleted", tf->name);
                tf->watchfd = -1;
                tf_cnt_exist--;
                if (timeout < 0)
                    timeout = LOG_CREATE_SCAN_INTERVAL;
            }
            e += sizeof(struct inotify_event) + ev->len;
        }
    }

    reset_list_free(reset_list, reset_item_free);

    log_close(logh);
    free_config();
    printf("freeing tables: %p\n", pcre_tables);
    if (pcre_tables)
        pcre_free((void *)pcre_tables);

    return EXIT_SUCCESS;
}
