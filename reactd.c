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

#ifdef SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include "avl.h"
#include "expire_list.h"
#include "log.h"
#include "reactd.h"

// from reactd_conf.y
extern tfile **tf;
extern unsigned int tf_cnt;
#ifdef SYSTEMD
extern tjournal **tj;
extern unsigned int tj_cnt;
#endif
extern tglobal_cfg cfg;

unsigned char *pcre_tables = NULL;

log_h *logh; // log handle
texpire_list *resets = NULL; // keep track of pending resets (this is shared among all files/res)
texpire_list *expires = NULL; // keep track of hits in file->res->hitlist to expire if no new hits are recorded in INTERVAL period (we store keyhits *)

// int timeout = -1; // poll timeout for all files, -1 if all monitored files exist, otherwise LOG_CREATE_SCAN_INTERVAL

static volatile sig_atomic_t exit_flag = 0;

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
        logw(logh, LOG_DEBUG, "Running %s", argv[0]);
        execv(argv[0], argv);
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
    for (unsigned int i = 0; ((treset *)reset)->argv[i]; i++)
        free(((treset *)reset)->argv[i]);
    free(((treset *)reset)->argv);

    for (unsigned int i = 0; i < ((treset *)reset)->env->cnt; i++) {
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
    setenv("REACT_SOURCE", ((treset *)reset)->source, 1);
    for (unsigned int i = 0; i < ((treset *)reset)->env->cnt; i++)
        setenv(((treset *)reset)->env->names[i], ((treset *)reset)->env->values[i], 1);

    run_prog(((treset *)reset)->argv);

    if (((treset *)reset)->hits)
        unsetenv("REACT_KEY");
    for (unsigned int i = 0; i < ((treset *)reset)->env->cnt; i++)
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

// note: this only works for x>=0
#define num_digits(x) (unsigned int)(x==0?1:(1+log10(x)))

void proc_re(char *source, re *re, char *msg) {
    int *filter_matches = malloc(3 * re->capture_cnt * sizeof(int));

    logw(logh, LOG_DEBUG, "matching %s re %s against %s", source, re->str, msg);
    int filter_match_cnt = pcre_exec(re->re, re->studied, msg, strlen(msg), 0, 0, filter_matches, 3 * re->capture_cnt);
    if (filter_match_cnt > 0) {
        logw(logh, LOG_INFO, "%s matched RE %s count=%d", source, re->str, filter_match_cnt);

        int run = 0;
        char *key = NULL;
        keyhits *hits = NULL;

        if (re->key != NULL) {
            key = pcre_subst_replace(msg, re->key, filter_matches, 3 * re->capture_cnt, filter_match_cnt, 0);
            logw(logh, LOG_DEBUG, "Got key: %s 0x%X", key, key);
        }

        // if we have to keep track of these hits (eg: we have a key and trigger)
        if (re->trigger_cnt > 0) {
            logw(logh, LOG_DEBUG, "Checking trigger condition for key %s", key);
            texpire *expire = malloc(sizeof(texpire));
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
                    logw(logh, LOG_DEBUG, "%s RE %s triggered by %u hits in %lu seconds", source, re->str, ring_get_size(hits->hits), *hit_time - *hit_first);
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
            setenv("REACT_SOURCE", source, 1);

            for (unsigned int i = 0; i < filter_match_cnt; i++) {
                char *v = strndup(&msg[filter_matches[2*i]], filter_matches[2*i+1] - filter_matches[2*i]);
                char k[7 + num_digits(i)];
                sprintf(k, "REACT_%u", i);
                setenv(k, v, 1);
                free(v);
            }

            // build argv for execv()
            char **argv = (char **)malloc((re->cmd->cnt + 1) * sizeof(char *));
            assert(argv != NULL);
            for (unsigned int i = 0; i < re->cmd->cnt; i++)
                argv[i] = pcre_subst_replace(msg, re->cmd->args[i], filter_matches, 3 * re->capture_cnt, filter_match_cnt, 0);
            argv[re->cmd->cnt] = NULL; // null-terminate for execv()

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
                    reset->source = source; // source pointer in tf (eg: filename or journal)
                    reset->argv = (char **)malloc((re->reset_cmd->cnt + 1) * sizeof(char *)); // build argv for execv()
                    assert(reset->argv != NULL);
                    for (unsigned int i = 0; i < re->cmd->cnt; i++)
                        reset->argv[i] = pcre_subst_replace(msg, re->reset_cmd->args[i], filter_matches, 3 * re->capture_cnt, filter_match_cnt, 0);
                    reset->argv[re->reset_cmd->cnt] = NULL; // null-terminate for execv()

                    // prepare env vars for reset command
                    reset->env = malloc(sizeof(tenv));
                    reset->env->names = malloc(filter_match_cnt * sizeof(char **));
                    reset->env->values = malloc(filter_match_cnt * sizeof(char **));
                    for (unsigned int i = 0; i < filter_match_cnt; i++) {
                        reset->env->names[i] = malloc(7 + num_digits(i));
                        sprintf(reset->env->names[i], "REACT_%u", i);
                        reset->env->values[i] = strndup(&msg[filter_matches[2*i]], filter_matches[2*i+1] - filter_matches[2*i]);
                    }
                    reset->env->cnt = filter_match_cnt;

                    expire_list_add(resets, reset, re->reset_time);

                }
            }
            for (unsigned int i = 0; i < re->cmd->cnt; i++)
                free(argv[i]);
            free(argv);
            unsetenv("REACT_SOURCE");
            if (key)
                unsetenv("REACT_KEY");
            for (unsigned int i = 0; i < filter_match_cnt; i++) {
                char k[8];
                snprintf(k, 8, "REACT_%u", i);
                unsetenv(k);
            }
        }

        if (key)
            free(key);

    }
    free(filter_matches);
}

// callback for tail_lines
// runs record_hit function
void proc_line(tfile *tf, char *msg) {
    for (unsigned int ri = 0; ri < tf->re_cnt; ri++)
        proc_re(tf->name, tf->re[ri], msg);
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

#ifdef SYSTEMD
void proc_journal(tjournal *tj, char *msg) {
    for (unsigned int ri = 0; ri < tj->re_cnt; ri++)
        proc_re("journal", tj->re[ri], msg);
}

void tail_journal(sd_journal *j) {
    if (0 > sd_journal_process(j)) {
        logw(logh, LOG_ERR, "Error processing journal events: %s", strerror(errno));
        return;
    }
    while (sd_journal_next(j) > 0) {
        const void *d;
        size_t l;
        unsigned int cnt = 0;

        int filter_match = 1;
        for (unsigned int tji = 0; filter_match && tji < tj_cnt; tji++) {
            tj_filters *jf = tj[tji]->filters;
            const void *data;
            void *value;
            size_t len;

            //  check if all filters match for this tj
            for (unsigned int jfi = 0; jfi < jf->cnt; jfi++) {
                if (0 > sd_journal_get_data(j, jf->fields[jfi], &data, &len)) {
                    logw(logh, LOG_ERR, "Error getting data from journal: %s", strerror(errno));
                    filter_match = 0;
                    break;
                }
                value = index(data, '=') + 1;

                if (len - (value - data) != strlen(jf->values[jfi]) || strncmp(jf->values[jfi], value, len - (value - data))) {
                    filter_match = 0;
                    break;
                }
            }
            if (filter_match) {
                // filters matched tj, now check all regexes for this tj
                if (0 > sd_journal_get_data(j, "MESSAGE", &data, &len)) {
                    logw(logh, LOG_ERR, "Error getting message from journal: %s", strerror(errno));
                } else {
                    value = index(data, '=') + 1;
                    char *msg = strndup(value, len - (value - data));
                    proc_journal(tj[tji], msg);
                    free(msg);
                }

            }
        }

    }
}
#endif

// main loop
// at this point we have all the config and log initialized
int react_main() {
    struct pollfd pw[POLL_FD_CNT]; // used to store: 0:inotify and 1:journal file descriptors and to poll for read/delete events
    int timeout = -1; // for poll(), will be the smallest after considering reset_list, expire_hits and LOG_CREATE_INTERVAL
    unsigned int tf_cnt_exist = 0; // number of monitored files that exist

    logw(logh, LOG_INFO, "Starting reactd");

    dprint("Global options:\nversion: %u.%u\npidfile: %s\nlogging: %s\nloglevel: %s\n", cfg.version_major, cfg.version_minor, cfg.pidfile, logdst_str(cfg.logdst), loglevel_str(cfg.loglevel));

    if (tf_cnt > 0) {
        // Initialize global inotify fd and poll structure
        pw[POLL_FD_INOTIFY].fd = inotify_init();
        pw[POLL_FD_INOTIFY].events = POLLIN;

        if (pw[POLL_FD_INOTIFY].fd == -1) {
            logw(logh, LOG_ERR, "Error in inotify_init(): %s", strerror(errno));
            return EXIT_FAILURE;
        }

        struct stat st;
        for (unsigned int i = 0; i < tf_cnt; i++) {
            tf[i]->watchfd = inotify_add_watch(pw[0].fd, tf[i]->name, IN_MODIFY|IN_IGNORED);
            if (-1 == stat(tf[i]->name, &st)) {
                // file to monitor doesn't exist
                timeout = LOG_CREATE_SCAN_INTERVAL;
                logw(logh, LOG_DEBUG, "%s doesn't exist, new timeout: %d", tf[i]->name, timeout);
                // tfs[i]->pos = 0;
            } else {
                tf_cnt_exist++;
                tf[i]->pos = st.st_size;
                logw(logh, LOG_DEBUG, "%s position: %lu", tf[i]->name, tf[i]->pos);
            }
        }
    } else {
#ifdef SYSTEMD
            pw[POLL_FD_INOTIFY].fd = -1;
            logw(logh, LOG_DEBUG, "Not monitoring any files");
#else
            logw(logh, LOG_ERR, "Nothing to monitor");
            return EXIT_FAILURE;
#endif
    }

#ifdef SYSTEMD
    sd_journal *j = NULL;
    if (tj_cnt > 0) {
        int r;

        if (0 > sd_journal_open(&j, SD_JOURNAL_SYSTEM)) {
            logw(logh, LOG_ERR, "Error opening journal: %m");
            return EXIT_FAILURE;
        }
        if (0 > sd_journal_seek_tail(j)) {
            logw(logh, LOG_ERR, "Error seeking in journal: %m");
            return EXIT_FAILURE;
        }
        if (0 > sd_journal_next(j)) {
            logw(logh, LOG_ERR, "Error moving in journal: %m");
            return EXIT_FAILURE;
        }
        pw[POLL_FD_JOURNAL].fd = sd_journal_get_fd(j);
        pw[POLL_FD_JOURNAL].events = POLLIN;
    } else {
        logw(logh, LOG_DEBUG, "Not monitoring journal");
        if (tf_cnt == 0) {
            logw(logh, LOG_ERR, "Nothing to monitor");
            return EXIT_FAILURE;
        }
        pw[POLL_FD_JOURNAL].fd = -1;
    }
#endif

    resets  = expire_list_init(NULL);
    expires = expire_list_init(expire_cmp);

    char inotify_buf[ 100 * (sizeof(struct inotify_event) + NAME_MAX + 1)];

    while (!exit_flag) {
        logw(logh, LOG_DEBUG, "Monitors: %u/%u sleeping %d milliseconds", tf_cnt_exist, tf_cnt, timeout);
        int pollret = poll(pw, POLL_FD_CNT, timeout);

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

            if (tf_cnt_exist < tf_cnt) {
                // check if file has been created
                for (unsigned int i = 0; i < tf_cnt; i++) {
                    if (tf[i]->watchfd == -1) {
                        tf[i]->watchfd = inotify_add_watch(pw[0].fd, tf[i]->name, IN_MODIFY|IN_IGNORED);
                        if (tf[i]->watchfd == -1) {
                            logw(logh, LOG_DEBUG, "%s not monitored: %m", tf[i]->name);
                        } else {
                            tf_cnt_exist++;
                            logw(logh, LOG_DEBUG, "%s monitoring from now", tf[i]->name);
                            // tf->pos = 0;
                            tail_lines(tf[i], proc_line);
                        }
                    }
                }
            }

            // fall through to update timeouts
        }


        if (pollret > 0) {
            // inotify event or something new in journal

            if (pw[POLL_FD_INOTIFY].revents) {
                if (pw[POLL_FD_INOTIFY].revents & POLLIN) {
                    ssize_t len = read(pw[0].fd, inotify_buf, sizeof(inotify_buf));
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
                        tfile *cf = NULL;
                        for (unsigned int fi = 0; fi < tf_cnt; fi++) {
                            if (tf[fi]->watchfd == ev->wd) {
                                cf = tf[fi];
                                logw(logh, LOG_DEBUG, "%s got event", cf->name);

                                if (ev->mask & IN_MODIFY)
                                    tail_lines(cf, proc_line);

                                if (ev->mask & IN_IGNORED) {
                                    logw(logh, LOG_DEBUG, "%s deleted", cf->name);
                                    cf->watchfd = -1;
                                    tf_cnt_exist--;
                                    if (timeout < 0)
                                        timeout = LOG_CREATE_SCAN_INTERVAL;
                                }
                            }
                        }

                        e += sizeof(struct inotify_event) + ev->len;
                    }
                } else {
                    logw(logh, LOG_ERR, "Error polling inotify: %s", strerror(errno));
                }
            }
#ifdef SYSTEMD
            if (pw[POLL_FD_JOURNAL].revents) {
                tail_journal(j);
            } else {
                logw(logh, LOG_ERR, "Error polling journal: %s", strerror(errno));
            }
#endif
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
        if (tf_cnt_exist < tf_cnt && timeout * 1000 < LOG_CREATE_SCAN_INTERVAL)
            timeout = LOG_CREATE_SCAN_INTERVAL;
    }

#ifdef SYSTEMD
    if (j != NULL)
        sd_journal_close(j);
#endif

    return EXIT_SUCCESS;
}

// signal handler to stop react_main()
void react_stop(int sig) {
    logw(logh, LOG_NOTICE, "Received signal %d, exiting", sig);
    exit_flag = 1;
}

void free_re(re *re) {
    dprint("    freeing RE %s\n", re->str);
    free(re->str);
    if (re->studied)
        pcre_free_study(re->studied);
    if (re->re)
        pcre_free(re->re);
    if (re->cmd) {
        dprint("freeing cmd");
        for (unsigned int ci = 0; ci < re->cmd->cnt; ci++)
            pcre_subst_free(re->cmd->args[ci]);
        free(re->cmd->args);
        free(re->cmd);
    }
    if (re->reset_cmd) {
        dprint("freeing reset_cmd");
        for (unsigned int i = 0; i < re->reset_cmd->cnt; i++)
            pcre_subst_free(re->reset_cmd->args[i]);
        free(re->reset_cmd->args);
        free(re->reset_cmd);
    }
    if (re->key)
        pcre_subst_free(re->key);
    if (re->hitlist)
        avl_destroy(re->hitlist, free_keyhits);
    free(re);
}

void free_config() {
    if (cfg.pidfile)
        free(cfg.pidfile);
    if (cfg.logfile)
        free(cfg.logfile);
    if (cfg.logprefix)
        free(cfg.logprefix);

    // free tailed files
    for (unsigned int fi = 0; fi < tf_cnt; fi++) {
        dprint("freeing TF %s", tf[fi]->name);
        free(tf[fi]->name);
        for (unsigned int ri = 0; ri < tf[fi]->re_cnt; ri++)
            free_re(tf[fi]->re[ri]);
        free(tf[fi]->re);
        free(tf[fi]);
    }
    dprint("freeing tf: %p", tf);
    free(tf);

#ifdef SYSTEMD
    // free tailed journals
    for (unsigned int ji = 0; ji < tj_cnt; ji++) {
        dprint("freeing TJ %u", ji);
        for (unsigned int jfi = 0; jfi < tj[ji]->filters->cnt; jfi++) {
            dprint("freeing TJ %u filter field %u: %p", ji, jfi, tj[ji]->filters->fields[jfi]);
            free(tj[ji]->filters->fields[jfi]);
            dprint("freeing TJ %u filter value %u: %p", ji, jfi, tj[ji]->filters->values[jfi]);
            free(tj[ji]->filters->values[jfi]);
        }
        dprint("freeing TJ %u filters: %p", ji, tj[ji]->filters);
        free(tj[ji]->filters->fields);
        free(tj[ji]->filters->values);
        free(tj[ji]->filters);
        for (unsigned int ri = 0; ri < tj[ji]->re_cnt; ri++)
            free_re(tj[ji]->re[ri]);
        dprint("freeing TJ %u: %p", ji, tj[ji]);
        free(tj[ji]->re);
        free(tj[ji]);
    }
    dprint("freeing tj: %p", tj);
    free(tj);
#endif
}

int main(int argc, char **argv) {
    dprint_init();
    char *config = DEFAULT_CONFIG;
    int ch;
    int ret = 0;

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

        // must be called before parse_config()
        pcre_tables = (unsigned char *)pcre_maketables();

        memset(&cfg, 0, sizeof(cfg));
        ret |= parse_config(config);

        if (ret == 0) {
            logh = log_open(cfg.logdst, cfg.loglevel, cfg.logprefix, cfg.logfile);
            signal(SIGCHLD, SIG_IGN);
            signal(SIGINT,  react_stop);
            signal(SIGTERM, react_stop);
            ret |= react_main();
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT,  SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            log_close(logh);
        }

        free_config();

        dprint("freeing memory");
        if (resets)
            expire_list_free(resets, reset_free);
        if (expires)
            expire_list_free(expires, expires_run);

        if (pcre_tables)
            pcre_free((void *)pcre_tables);

    return ret;
}
