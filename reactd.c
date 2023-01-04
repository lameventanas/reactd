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

#include "avl.h"
#include "expire_list.h"
#include "log.h"
#include "reactd.h"

extern tfile *tfs;

unsigned char *pcre_tables = NULL;

log_h *logh; // log handle
texpire_list *resets = NULL; // keep track of pending resets (this is shared among all files/res)
texpire_list *expires = NULL; // keep track of hits in file->res->hitlist to expire if no new hits are recorded in INTERVAL period (we store keyhits *)

// int timeout = -1; // poll timeout for all files, -1 if all monitored files exist, otherwise LOG_CREATE_SCAN_INTERVAL

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


int run_prog(char **argv) {
    pid_t pid = fork();
    if (pid == 0) {
        // if (daemon(0, 0) < 0) {
        if (daemon(0, 1) < 0) {
            logw(logh, LOG_ERR, "daemon: %m");
            exit(1);
        }
        printf("Running %s\n", argv[0]);
        execv(argv[0], argv);
        printf("Could not run %s: %s\n", strerror(errno));
        logw(logh, LOG_ERR, "Could not run %s: %m", argv[0]);
        exit(1);
    }
    if (pid < 0) {
        logw(logh, LOG_ERR, "Could not fork: %m");
        return 0;
    }
    return 1;
}

void reset_free(void *reset) {
    printf("freeing reset item 0x%X\n", reset);

    for (unsigned int i = 0; ((treset *)reset)->argv[i]; i++)
        free(((treset *)reset)->argv[i]);
    free(((treset *)reset)->argv);

    for (unsigned int i = 0; i < ((treset *)reset)->env->len; i++) {
        free(((treset *)reset)->env->names[i]);
        free(((treset *)reset)->env->values[i]);
    }
    free(((treset *)reset)->env->names);
    free(((treset *)reset)->env->values);
    free(((treset *)reset)->env);

    if (((treset *)reset)->key)
        free(((treset *)reset)->key);

    free(reset);
}

// callback compare for avl tree
int keyhits_cmp(const void *a, const void *b, void *param) {
    return strcmp( ((keyhits *)a)->key, ((keyhits *)b)->key);
}
void free_keyhits(void *kh, void *param) {
    free(((keyhits *)kh)->key);
    ring_free(((keyhits *)kh)->hits, free);
}

// run reset for a specific key
void resets_run(void *reset) {
    if (((treset *)reset)->key) {
        logw(logh, LOG_DEBUG, "Resetting item with key %s", ((treset *)reset)->key);
        setenv("REACT_KEY", ((treset *)reset)->key, 1);
    }
    else {
        logw(logh, LOG_DEBUG, "Resetting item");
    }
    setenv("REACT_FILE", ((treset *)reset)->logfile, 1);
    for (unsigned int i = 0; i < ((treset *)reset)->env->len; i++)
        setenv(((treset *)reset)->env->names[i], ((treset *)reset)->env->values[i], 1);

    run_prog(((treset *)reset)->argv);

    if (((treset *)reset)->hits)
        unsetenv("REACT_KEY");
    for (unsigned int i = 0; i < ((treset *)reset)->env->len; i++)
        unsetenv(((treset *)reset)->env->names[i]);

    reset_free((treset *)reset);
}

int expire_cmp(const void *a, const void *b) {
    return (
        (((texpire *)a)->re == ((texpire *)b)->re) &&
        (strcmp(((texpire *)a)->hits->key, ((texpire *)b)->hits->key) == 0)
    ) ? 0 : 1;
}

// expire items in avl after the period time has passed and no further hits were detected
// NOTE: nothing really runs, this only frees memory in the avl and the expire_list itself
void expires_run(void *expire) {
    logw(logh, LOG_DEBUG, "Expiring keyhits for RE %s key %s", ((texpire *)expire)->re->str, ((texpire *)expire)->hits->key);

    // remove reference from avl
    avl_delete(((texpire *)expire)->re->hitlist, ((texpire *)expire)->hits);

    // free memory from keyhits structure
    free_keyhits(((texpire *)expire)->hits, NULL);

    free(((texpire *)expire)->hits);

    free(expire);
}

void free_config() {
    if (cfg.pidfile)
        free(cfg.pidfile);
    if (cfg.logfile)
        free(cfg.logfile);
    if (cfg.logprefix)
        free(cfg.logprefix);

    for (tfile *tf = tfs; tf->name; tf++) {
        dprint("freeing TF %s %p\n", tf->name, tf->name);
        free(tf->name);
        for (re *re = tf->re; re->str; re++) {
            dprint("    freeing RE %s %p\n", re->str, re->str);
            free(re->str);
            if (re->re)
                pcre_free(re->re);
            if (re->cmd) {
                dprint("freeing cmd");
                for (unsigned int i = 0; i < re->cmd->len; i++) {
#ifdef DEBUG
                    {
                        char *tmp = pcre_subst_str(re->cmd->args[i]);
                        dprint("freeing cmd arg %u: %s", i, tmp);
                        free(tmp);
                    }
#endif
                    pcre_subst_free(re->cmd->args[i]);
                }
                free(re->cmd->args);
                free(re->cmd);
            }
            if (re->reset_cmd) {
                dprint("freeing reset_cmd");
                for (unsigned int i = 0; i < re->reset_cmd->len; i++) {
#ifdef DEBUG
                    {
                        char *tmp = pcre_subst_str(re->reset_cmd->args[i]);
                        dprint("freeing reset_cmd arg %u: %s", i, tmp);
                        free(tmp);
                    }
#endif
                    pcre_subst_free(re->reset_cmd->args[i]);
                }
                free(re->reset_cmd->args);
                free(re->reset_cmd);
            }
            if (re->re_studied)
                pcre_free_study(re->re_studied);
            if (re->key)
                pcre_subst_free(re->key);
            if (re->hitlist)
                avl_destroy(re->hitlist, free_keyhits);
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

        // TODO: move capture_cnt to tf structure?
        pcre_fullinfo(re->re, re->re_studied, PCRE_INFO_CAPTURECOUNT, &capture_cnt);
        capture_cnt++; // add space for \0
        int *matches = malloc(3 * capture_cnt * sizeof(int));

        logw(logh, LOG_DEBUG, "matching re %s against %s", re->str, s);
        int match_cnt = pcre_exec(re->re, re->re_studied, s, strlen(s), 0, 0, matches, 3 * capture_cnt);
        if (match_cnt > 0) {
            logw(logh, LOG_INFO, "%s matched RE %s count=%d", tf->name, re->str, match_cnt);

            int run = 0;
            char *key = NULL;
            keyhits *hits = NULL;

            if (re->key != NULL) {
                key = pcre_subst_replace(s, re->key, matches, 3*capture_cnt, match_cnt, 0);
                logw(logh, LOG_DEBUG, "Got key: %s 0x%X", key, key);
            }

            // if we have to keep track of these hits (eg: we have a key and trigger)
            if (re->trigger_cnt > 0) {
                logw(logh, LOG_DEBUG, "Checking trigger condition for key %s", key);
                texpire *expire = malloc(sizeof(texpire));
                assert(expire != NULL);
                expire->re = re;

                keyhits search = {
                    .key = key,
                    .hits = NULL
                };

                hits = avl_find(re->hitlist, &search);
                if (!hits) {
                    logw(logh, LOG_DEBUG, "Creating hit list for %s", key);
                    hits = malloc(sizeof(keyhits));
                    hits->key = strdup(key);
                    hits->hits = ring_init(re->trigger_cnt);
                    avl_insert(re->hitlist, hits);
                    // expire hits after trigger_time
                    // ie: if the trigger is 5 in 10 minutes, and 10 minutes have passed with no hits, remove from avl

                    expire->hits = hits;
                    expire_list_add(expires, expire, re->trigger_time);
                } else {
                    expire->hits = hits;
                    expire_list_set(expires, expire, re->trigger_time);
                    // free expire because we already have one in expires list that we will update instead of inserting this one
                    free(expire);
                }

                time_t *hit_time = malloc(sizeof(time_t));
                time(hit_time);
                time_t *old_time = ring_put(hits->hits, (void *)hit_time);
                if (old_time != NULL)
                    free(old_time);

                if (ring_count(hits->hits) == re->trigger_cnt) {
                    time_t *hit_first = ring_get_oldest(hits->hits, 0);
                    if (*hit_time - *hit_first <= re->trigger_time) {
                        logw(logh, LOG_DEBUG, "%s RE %s triggered by %u hits in %lu seconds", tf->name, re->str, ring_get_size(hits->hits), *hit_time - *hit_first);
                        run = 1;
                    }
                }
            } else {
                logw(logh, LOG_DEBUG, "Running command without checking trigger condition");
                run = 1;
            }

            if (run) {
                if (key) {
                    logw(logh, LOG_DEBUG, "Running command for key %s", key);
                    setenv("REACT_KEY", key, 1);
                } else {
                    logw(logh, LOG_DEBUG, "Running command without key");
                }
                setenv("REACT_FILE", tf->name, 1);

                for (unsigned int i = 0; i < match_cnt; i++) {
                    char *v = strndup(&s[matches[2*i]], matches[2*i+1] - matches[2*i]);
                    char k[7 + num_digits(i)];
                    sprintf(k, "REACT_%u", i);
                    setenv(k, v, 1);
                    free(v);
                }

                // build argv for execv()
                char **argv = (char **)malloc((re->cmd->len + 1) * sizeof(char *));
                assert(argv != NULL);
                for (unsigned int i = 0; i < re->cmd->len; i++)
                    argv[i] = pcre_subst_replace(s, re->cmd->args[i], matches, 3*capture_cnt, match_cnt, 0);
                argv[re->cmd->len] = NULL; // null-terminate for execv()

                if (key)
                    logw(logh, LOG_INFO, "Running %s for %s", argv[0], key);
                else
                    logw(logh, LOG_INFO, "Running %s", argv[0]);

                if (run_prog(argv)) {
                    if (re->reset_cmd != NULL && re->reset_time > 0) {
                        if (key)
                            logw(logh, LOG_DEBUG, "Will reset key %s after %u seconds", key, re->reset_time);
                        else
                            logw(logh, LOG_DEBUG, "Will reset after %u seconds", re->reset_time);
                        treset *reset = malloc(sizeof(treset));
                        assert(reset != NULL);
                        reset->key = key == NULL ? NULL : strdup(key);
                        reset->hits = hits; // hits pointer in avl, or NULL when there's no key and trigger_time
                        reset->logfile = tf->name; // logfile pointer in tf
                        reset->argv = (char **)malloc((re->reset_cmd->len + 1) * sizeof(char *)); // build argv for execv()
                        assert(reset->argv != NULL);
                        for (unsigned int i = 0; i < re->cmd->len; i++)
                            reset->argv[i] = pcre_subst_replace(s, re->reset_cmd->args[i], matches, 3*capture_cnt, match_cnt, 0);
                        reset->argv[re->reset_cmd->len] = NULL; // null-terminate for execv()

                        // prepare env vars for reset command
                        reset->env = malloc(sizeof(tenv));
                        reset->env->names = malloc(match_cnt * sizeof(char **));
                        reset->env->values = malloc(match_cnt * sizeof(char **));
                        for (unsigned int i = 0; i < match_cnt; i++) {
                            reset->env->names[i] = malloc(7 + num_digits(i));
                            sprintf(reset->env->names[i], "REACT_%u", i);
                            reset->env->values[i] = strndup(&s[matches[2*i]], matches[2*i+1] - matches[2*i]);
                        }
                        reset->env->len = match_cnt;

                        expire_list_add(resets, reset, re->reset_time);

                    }
                }
                for (unsigned int i = 0; i < re->cmd->len; i++)
                    free(argv[i]);
                free(argv);
                unsetenv("REACT_FILE");
                if (key)
                    unsetenv("REACT_KEY");
                for (unsigned int i = 0; i < match_cnt; i++) {
                    char k[8];
                    snprintf(k, 8, "REACT_%u", i);
                    unsetenv(k);
                }
            }

            if (key)
                free(key);

        }
        free(matches);
    }
}

void tail_lines(tfile *tf, void (*callback)(tfile *, char *)) {
    struct stat st;
    int fd;

    usleep(EVENT_REACT_TIME);
    if (-1 == stat(tf->name, &st)) {
        logw(logh, LOG_ERR, "stat: %s: %m", tf->name);
        return;
    }

    logw(logh, LOG_DEBUG, "tail pos: %ld size: %ld", tf->pos, st.st_size);

    if (st.st_size == 0) {
        logw(logh, LOG_WARNING, "File reset: %s", tf->name);
        tf->pos = 0;
        return;
    }
    if (st.st_size < tf->pos) {
        logw(logh, LOG_WARNING, "File shrunk, will reset: %s", tf->name);
        tf->pos = 0;
    }
    if (st.st_size == tf->pos) {
        logw(logh, LOG_DEBUG, "Size didn't change: %s", tf->name);
        return;
    }

    fd = open(tf->name, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        logw(logh, LOG_ERR, "open: %s: %m", tf->name);
        return;
    }

#ifdef USE_MMAP
    logw(logh, LOG_DEBUG, "using mmap");

    ssize_t len = st.st_size - tf->pos;
    off_t tf_off = tf->pos % sysconf(_SC_PAGE_SIZE);
    char *addr = mmap(NULL, len + tf_off, PROT_READ, MAP_SHARED, fd, tf->pos - tf_off);
    if (addr == MAP_FAILED) {
        logw(logh, LOG_ERR, "mmap: %m");
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
            logw(logh, LOG_DEBUG, line);
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
            logw(logh, LOG_DEBUG, "found newline at pos=%ld sz=%ld", tf->pos, sz);
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
            logw(logh, LOG_DEBUG, "%s", line);
            (*callback)(tf, line);
            free(line);
            tf->pos += sz;
            sz = 0;
        }
    }

#endif

    close(fd);
    logw(logh, LOG_DEBUG, "new tail pos: %ld size: %ld", tf->pos, st.st_size);
}

void prog_exit(int sig) {
    logw(logh, LOG_NOTICE, "Received signal %d, exiting", sig);
    exit_flag = 1;
}

int main(int argc, char **argv) {
    dprint_init();
    char *config = DEFAULT_CONFIG;
    int ch;

    static const struct option longopts[] = {
        { "config",       required_argument, 0, 'c' },
        { "pidfile",      required_argument, 0, 'p' },
        { "statefile",    required_argument, 0, 's' },
        { "version",      no_argument,       0, 'V' },
        { "help",         no_argument,       0, 'h' },
        { NULL, 0, 0, 0 }
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

    logh = log_open(cfg.logdst, cfg.loglevel, cfg.logprefix, cfg.logfile);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, prog_exit);
    signal(SIGTERM, prog_exit);

    logw(logh, LOG_INFO, "Starting reactd");

    // pcre vars
    pcre_tables = (unsigned char *)pcre_maketables();
    // printf("pcre_tables: %p\n", pcre_tables);
    const char *error_msg;
    int error_off;

    dprint("Global options:\nversion: %u.%u\npidfile: %s\nlogging: %s\nloglevel: %s\n", cfg.version_major, cfg.version_minor, cfg.pidfile, logdst_str(cfg.logdst), loglevel_str(cfg.loglevel));

    // Initialize global inotify fd and poll structure
    pollwatch.fd = inotify_init();
    pollwatch.events = POLLIN;

    // TODO: sd_journal_open() then sd_journal_get_events() to get logs from journald and put it in pollwatch

    if (pollwatch.fd == -1) {
        logw(logh, LOG_ERR, "Error in inotify_init(): %s", strerror(errno));
        exit(1);
    }

    resets = expire_list_init(NULL);
    expires = expire_list_init(expire_cmp);

    struct stat st;
    unsigned int tf_cnt_exist = 0; // number of monitored files that exist
    unsigned int tf_cnt_total = 0; // number of monitored files
    int timeout = -1; // for poll(), will be the smallest after considering reset_list, expire_hits and LOG_CREATE_INTERVAL

    for (tfile *tf = tfs; tf->name; tf++) {
        tf_cnt_total++;
        tf->watchfd = inotify_add_watch(pollwatch.fd, tf->name, IN_MODIFY|IN_IGNORED);
        if (-1 == stat(tf->name, &st)) {
            // file to monitor doesn't exist
            timeout = LOG_CREATE_SCAN_INTERVAL;
            logw(logh, LOG_DEBUG, "%s doesn't exist, new timeout: %d", tf->name, timeout);
            tf->pos = 0;
        } else {
            tf_cnt_exist++;
            tf->pos = st.st_size;
            logw(logh, LOG_DEBUG, "%s position: %lu", tf->name, tf->pos);
        }

        // for (int i = 0; tf->re[i].str; i++) printf("    re: %p %s\n", tf->re[i], tf->re[i].str);
    }

    char inotify_buf[ 100 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    // exit_flag =1 ;
    while (!exit_flag) {
        logw(logh, LOG_DEBUG, "Monitors: %u/%u sleeping %d milliseconds", tf_cnt_exist, tf_cnt_total, timeout);
        int pollret = poll(&pollwatch, 1, timeout);

        // error or signal received
        if (pollret < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                logw(logh, LOG_DEBUG, "poll interrupted by signal");
                if (exit_flag)
                    break;
                // otherwise fall through to update timeout
            } else {
                logw(logh, LOG_ERR, "poll: %m");
                break;
            }
        }

        // timed out (host is ready to be unbanned, hits expired, or timed out waiting for log file to be created)
        if (pollret == 0) {

            // run pending resets / hit expirations
            logw(logh, LOG_DEBUG, "Running resets");
            expire_list_run(resets, resets_run);
            logw(logh, LOG_DEBUG, "Running expires");
            expire_list_run(expires, expires_run);

            if (tf_cnt_exist < tf_cnt_total) {
                // check if file has been created
                for (tfile *tf = tfs; tf->name; tf++) {
                    if (tf->watchfd == -1) {
                        tf->watchfd = inotify_add_watch(pollwatch.fd, tf->name, IN_MODIFY|IN_IGNORED);
                        if (tf->watchfd == -1) {
                            logw(logh, LOG_DEBUG, "%s not monitored: %m", tf->name);
                        } else {
                            tf_cnt_exist++;
                            logw(logh, LOG_DEBUG, "%s monitoring from now", tf->name);
                            tf->pos = 0;
                            tail_lines(tf, proc_line);
                        }
                    }
                }
            }

            // fall through to update timeouts
        }


        if (pollret > 0) {
            // inotify event arrived
            ssize_t len = read(pollwatch.fd, inotify_buf, sizeof(inotify_buf));
            if (len < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                } else {
                    logw(logh, LOG_ERR, "Error reading inotify event: %m");
                    exit(1);
                }
            }
            for (int e = 0; e < len; ) {
                struct inotify_event *ev = (struct inotify_event *) &inotify_buf[e];
                logw(logh, LOG_DEBUG, "Inotify event pos %d/%ld: %#08x", e, len, ev->mask);

                // find out which monitored file received the event
                tfile *tf;
                for (tf = tfs; tf->name; tf++)
                    if (tf->watchfd == ev->wd)
                        break;
                assert(tf->name != NULL);
                logw(logh, LOG_DEBUG, "%s got event", tf->name);

                if (ev->mask & IN_MODIFY)
                    tail_lines(tf, proc_line);

                if (ev->mask & IN_IGNORED) {
                    logw(logh, LOG_DEBUG, "%s deleted", tf->name);
                    tf->watchfd = -1;
                    tf_cnt_exist--;
                    if (timeout < 0)
                        timeout = LOG_CREATE_SCAN_INTERVAL;
                }
                e += sizeof(struct inotify_event) + ev->len;
            }
        }

        // update poll() timeout
        timeout = -1;

        int t_expires = expire_list_next_expiracy(expires);
        logw(logh, LOG_DEBUG, "Next hit expiracy: %d", t_expires);

        int t_resets = expire_list_next_expiracy(resets);
        logw(logh, LOG_DEBUG, "Next reset run: %d", t_resets);

        if (t_expires > 0 || t_resets > 0) {
            if (t_expires > 0 && t_resets > 0) {
                // if both are set, take smallest timeout
                timeout = ((t_expires < t_resets) ? t_expires : t_resets) - time(NULL);
            } else {
                // if only one is set, take that one
                timeout = ((t_expires > 0) ? t_expires : t_resets) - time(NULL);
            }
        }

        // convert to milliseconds
        if (timeout > 0)
            timeout *= 1000;

        // if we are waiting for a log file to be created, and the timeout is smaller than the log_create_scan_interval, use that instead
        if (tf_cnt_exist < tf_cnt_total && timeout * 1000 < LOG_CREATE_SCAN_INTERVAL)
            timeout = LOG_CREATE_SCAN_INTERVAL;
    }

    dprint("freeing memory");
    expire_list_free(resets, reset_free);
    expire_list_free(expires, expires_run);

    log_close(logh);
    free_config();
    dprint("freeing tables: %p\n", pcre_tables);
    if (pcre_tables)
        pcre_free((void *)pcre_tables);

    return EXIT_SUCCESS;
}
