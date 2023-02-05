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
tfile **tf = NULL;       // tailed file configs, populated by parse_config()
unsigned int tf_cnt = 0; // number of items in tf, populated by parse_config()
#ifdef SYSTEMD
tjournal **tj = NULL;    // tailed journal configs, populated by parse_config()
unsigned int tj_cnt = 0; // number of items in tj, populated by parse_config()
#endif
tglobal_cfg cfg;         // global config settings, populated by parse_config()

unsigned char *pcre_tables = NULL;

log_h *logh; // log handle
texpire_list *resets = NULL;  // keep track of pending resets (this is shared among all files/res)
texpire_list *expires = NULL; // keep track of hits in file->res->hitlist to expire if no new hits are recorded in INTERVAL period (we store keyhits *)

static volatile sig_atomic_t exit_flag = 0;

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "\nUsage:\nreactd [option] file\n");

    fprintf(out,
        "\nOptions:\n"
        " -c --config FILE    configuration file\n"
        " -t --test           syntax test configuration file and exit\n"
        " -p --pidfile FILE   pid file\n"
        " -d --logdst         one of: syslog, file, stdout, stderr\n"
        " -f --logfile        output file when logging to a file\n"
        " -l --loglevel       one of: emerg, alert, crit, err, warn, notice, info, debug\n"
        " -V --version        output version information and exit\n"
        " -h --help           display this help and exit\n\n"
    );

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

#ifdef DEBUG

// Like ctime(), but without the last newline
// if dst is null, memory will be allocated for result and must be freed
char *my_ctime(time_t *t, char *dst) {
    if (dst == NULL)
        dst = malloc(30);

    if (*t < 0) {
        strcpy(dst, "unknown");
    } else {
        struct tm tmp;
        localtime_r(t, &tmp);
        strftime(dst, 30, "%F %T", &tmp);
    }
    return dst;
}

void print_time(void *t) {
    char s[30];
    my_ctime(t, s);
    printf("%d -> %s", *(time_t *)t, s);
}

void react_debug_re(re *re) {
    dprint("    RE: %s", re->str);
    if (re->hitlist) {
        dprint("    Hitlist: %u items", avl_count(re->hitlist));
        struct avl_traverser tr;

        unsigned int i = 0;
        avl_t_init(&tr, re->hitlist);

        for (keyhits *n = avl_t_first(&tr, re->hitlist); n != NULL; n = avl_t_next(&tr)) {
            char t_old[30];
            char t_new[30];
            my_ctime(ring_oldest(n->hits), t_old);
            my_ctime(ring_newest(n->hits), t_new);

            dprint("        %2u key: %-15s hits: %u/%u first: %s last: %s",
                ++i,
                n->key,
                ring_count(n->hits),
                ring_size(n->hits),
                t_old,
                t_new
            );
            // ring_print(n->hits, print_time);
        }
    } else {
        dprint("    No trigger configured");
    }
}
void react_debug() {
    // print status for tailed files
    for (unsigned int fi = 0; fi < tf_cnt; fi++) {
        dprint("TF %u: %s", fi, tf[fi]->name);
        for (unsigned int ri = 0; ri < tf[fi]->re_cnt; ri++) {
            dprint("    RE %u:", ri);
            react_debug_re(tf[fi]->re[ri]);
        }
    }
    // print status for journal
#ifdef SYSTEMD
    for (unsigned int ji = 0; ji < tj_cnt; ji++) {
        dprint("TJ %u", ji);
        for (unsigned int ri = 0; ri < tj[ji]->re_cnt; ri++) {
            dprint("    RE %u:", ri);
            react_debug_re(tj[ji]->re[ri]);
        }
    }
#endif

    dprint("Expire items:");
    unsigned int i = 0;
    for (texpire_item *n = expires->items; n; n = n->next) {
        char ts[30];
        texpire *expire = n->obj;
        dprint("    %u: re: %s key: %s t: %s", i++, expire->re->str, expire->hits->key, ts);
    }

    dprint("Reset items:");
    i = 0;
    for (texpire_item *n = resets->items; n; n = n->next) {
        char ts[30];
        treset *reset = n->obj;
        dprint("    %u: source: %s key: %s t: %s", i++, reset->source, reset->key, ts);
    }
}
#endif

int run_prog(char **argv) {
    pid_t pid = fork();
    if (pid == 0) {
        if (daemon(0, 1) < 0) {
            logw(logh, LOG_ERR, "daemon: %m");
            exit(1);
        }
        if (log_above(logh, LOG_DEBUG)) {
            char *cmd = calloc(1, sizeof(char));
            for (unsigned int i = 0; argv[i]; i++) {
                cmd = realloc(cmd, strlen(cmd) + strlen(argv[i]) + 2);
                strcat(cmd, argv[i]);
                if (argv[i+1] != NULL)
                    strcat(cmd, " ");
            }
            logw(logh, LOG_DEBUG, "Running: %s", cmd);
            free(cmd);
        }
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
    // NOTE: can't use log_write() here, logh might be closed
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
    dprint("Freeing keyhits for %s: key:%p hits:%p", ((keyhits *)kh)->key, ((keyhits *)kh)->key, ((keyhits *)kh)->hits);
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
    // NOTE: can't use logh here, this might run after log_close()
    dprint("Expiring keyhits for RE %s key: %s", ((texpire *)expire)->re->str, ((texpire *)expire)->hits->key);

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

    logw(logh, LOG_DEBUG, "Matching %s RE ->%s<- text: ->%s<-", source, re->str, msg);
    int re_ret = pcre_exec(re->re, re->studied, msg, strlen(msg), 0, 0, filter_matches, 3 * re->capture_cnt);
    if (re_ret > 0) {
        logw(logh, LOG_INFO, "Text in %s matched RE ->%s<-", source, re->str);

        int run = 0;
        char *key = NULL;
        keyhits *hits = NULL;

        if (re->key != NULL) {
            key = pcre_subst_replace(msg, re->key, filter_matches, 3 * re->capture_cnt, re_ret, 0);
            logw(logh, LOG_DEBUG, "Got key: %s", key);
        }

        // if we have to keep track of these hits (eg: we have a key and trigger)
        if (re->trigger_cnt > 0) {
            logw(logh, LOG_DEBUG, "Checking trigger condition for key: %s", key);
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
                    logw(logh, LOG_DEBUG, "%s RE %s triggered by %u hits in %lu seconds", source, re->str, ring_size(hits->hits), *hit_time - *hit_first);
                    run = 1;
                }
            }
        } else {
            // logw(logh, LOG_DEBUG, "Running command without trigger");
            run = 1;
        }

        if (run) {
            if (key) {
                // logw(logh, LOG_DEBUG, "Running command for key %s", key);
                setenv("REACT_KEY", key, 1);
            } /* else {
                logw(logh, LOG_DEBUG, "Running command without key");
            } */
            setenv("REACT_SOURCE", source, 1);

            for (unsigned int i = 0; i < re_ret; i++) {
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
                argv[i] = pcre_subst_replace(msg, re->cmd->args[i], filter_matches, 3 * re->capture_cnt, re_ret, 0);
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
                        reset->argv[i] = pcre_subst_replace(msg, re->reset_cmd->args[i], filter_matches, 3 * re->capture_cnt, re_ret, 0);
                    reset->argv[re->reset_cmd->cnt] = NULL; // null-terminate for execv()

                    // prepare env vars for reset command
                    reset->env = malloc(sizeof(tenv));
                    reset->env->names = malloc(re_ret * sizeof(char **));
                    reset->env->values = malloc(re_ret * sizeof(char **));
                    for (unsigned int i = 0; i < re_ret; i++) {
                        reset->env->names[i] = malloc(7 + num_digits(i));
                        sprintf(reset->env->names[i], "REACT_%u", i);
                        reset->env->values[i] = strndup(&msg[filter_matches[2*i]], filter_matches[2*i+1] - filter_matches[2*i]);
                    }
                    reset->env->cnt = re_ret;

                    expire_list_add(resets, reset, re->reset_time);

                }
            }
            for (unsigned int i = 0; i < re->cmd->cnt; i++)
                free(argv[i]);
            free(argv);
            unsetenv("REACT_SOURCE");
            if (key)
                unsetenv("REACT_KEY");
            for (unsigned int i = 0; i < re_ret; i++) {
                char k[8];
                snprintf(k, 8, "REACT_%u", i);
                unsetenv(k);
            }
        }

        if (key)
            free(key);

    } else {
        if (re_ret != PCRE_ERROR_NOMATCH) {
            logw(logh, LOG_ERR, "pcre_exec() returned %d", re_ret);
        }
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
    logw(logh, LOG_DEBUG, "%s new tail pos: %ld size: %ld", tf->name, tf->pos, st.st_size);
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
    int timeout = -1; // for poll(), will be the smallest after considering reset_list, expire_hits and LOG_CREATE_INTERVAL
    unsigned int tf_cnt_exist = 0; // number of monitored files that exist
    struct pollfd pw[POLL_FD_CNT]; // used to store: 0:inotify and 1:journal file descriptors and to poll for read/delete events
    memset(&pw, 0, POLL_FD_CNT*sizeof(struct pollfd));

    logw(logh, LOG_INFO, "Starting reactd, log level: %s", loglevel_str(logh->level));

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
#ifdef DEBUG
        /*
        if (log_above(logh, LOG_DEBUG))
            react_debug();
        */
#endif

        if (log_above(logh, LOG_DEBUG))
            if (timeout < 0)
                logw(logh, LOG_DEBUG, "Monitors: %u/%u sleeping indefinitely", tf_cnt_exist, tf_cnt);
            else
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

            // fall through to update poll timeout
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
                        for (unsigned int fi = 0; fi < tf_cnt; fi++) {
                            if (tf[fi]->watchfd == ev->wd) {
                                logw(logh, LOG_DEBUG, "%s got event", tf[fi]->name);

                                if (ev->mask & IN_MODIFY)
                                    tail_lines(tf[fi], proc_line);

                                if (ev->mask & IN_IGNORED) {
                                    logw(logh, LOG_DEBUG, "%s deleted", tf[fi]->name);
                                    tf[fi]->watchfd = -1;
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
            if (pw[POLL_FD_JOURNAL].revents & (POLLIN|POLLRDNORM) )
                tail_journal(j);
#endif
        }

        // update poll() timeout
        timeout = -1;

        time_t t_expires = expire_list_next_expiracy(expires);
        time_t t_resets  = expire_list_next_expiracy(resets);

#ifdef DEBUG
        if (log_above(logh, LOG_DEBUG)) {
            if (t_expires < 0)
                logw(logh, LOG_DEBUG, "Next hit expiracy: not scheduled");
            else {
                char s[30];
                my_ctime(&t_expires, s);
                logw(logh, LOG_DEBUG, "Next hit expiracy: %s", s);
            }
            if (t_resets < 0)
                logw(logh, LOG_DEBUG, "Next reset run: not scheduled");
            else {
                char s[30];
                my_ctime(&t_resets, s);
                logw(logh, LOG_DEBUG, "Next reset run: %s", s);
            }
        }
#endif

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
    int ch;
    int ret = 0;

    char *arg_config    = NULL;
    char arg_test       = 0;
    char arg_background = 0;
    char *arg_pidfile   = NULL;
    int arg_logdst      = -1;
    char *arg_logfile   = NULL;
    int arg_loglevel    = -1;

    static const struct option longopts[] = {
        { "config",       required_argument, 0, 'c' },
        { "test",         no_argument,       0, 't' },
        { "background",   no_argument,       0, 'b' },
        { "pidfile",      required_argument, 0, 'p' },
        { "logdst",       required_argument, 0, 'd' },
        { "logfile",      required_argument, 0, 'o' },
        { "loglevel",     required_argument, 0, 'l' },
        { "version",      no_argument,       0, 'V' },
        { "help",         no_argument,       0, 'h' },
        { NULL, 0, 0, 0 }
    };

    memset(&cfg, 0, sizeof(cfg));

    // will only be set by parse_config if they are not set by cmd args
    cfg.logdst   = LOG_INVALID;
    cfg.loglevel = LOG_INVALID;

    while ((ch = getopt_long(argc, argv, "c:tbp:d:o:l:Vh", longopts, NULL)) != -1)
        switch((char)ch) {
            case 'c':
                arg_config = optarg;
                break;
            case 't':
                arg_test = 1;
                break;
            case 'b':
                arg_background = 1;
                break;
            case 'p':
                arg_pidfile = optarg;
                break;
            case 'd':
                arg_logdst = logdst_int(optarg);
                if (arg_logdst == LOG_INVALID) {
                    fprintf(stderr, "Invalid log destintion\n");
                    usage(stderr);
                    exit(1);
                }
                break;
            case 'o':
                arg_logfile = optarg;
                break;
            case 'l':
                arg_loglevel = loglevel_int(optarg);
                if (arg_loglevel == LOG_INVALID) {
                    fprintf(stderr, "Invalid log level\n");
                    usage(stderr);
                    exit(1);
                }
                break;
            case 'V':
                printf("reactd version %s\n", VERSION);
                exit(0);
            case 'h':
                usage(stdout);
            default:
                usage(stderr);
        }
    if (arg_config == NULL) {
        fprintf(stderr, "No config file specifed\n");
        usage(stderr);
        exit(1);
    }

    // must be called before parse_config()
    pcre_tables = (unsigned char *)pcre_maketables();

    ret |= parse_config(arg_config);

    // args not set via command-line will be taken from config or defaults
    if (arg_logdst == -1)
        arg_logdst = (cfg.logdst != LOG_INVALID) ? cfg.logdst : DEFAULT_LOGDST;
    if (arg_loglevel == -1)
        arg_loglevel = (cfg.loglevel != LOG_INVALID) ? cfg.loglevel : DEFAULT_LOGLEVEL;
    if (arg_logdst == LOG_TO_FILE && arg_logfile == NULL)
        if (cfg.logfile)
            arg_logfile = cfg.logfile;
        else {
            fprintf(stderr, "No log file specified\n");
            usage(stderr);
            ret = 1;
        }

    if (ret == 0 && !arg_test) {
        logh = log_open(arg_logdst, arg_loglevel, cfg.logprefix, arg_logfile);
        dprint("Logging to %s with level %s", logdst_str(logh->dst), loglevel_str(logh->level));

        if (arg_background) {
            if (0 > daemon(0, 1)) {
                logw(logh, LOG_ERR, "Error becoming daemon: %s", strerror(errno));
                ret = 1;
            }
        }
        if (arg_pidfile) {
            FILE *fh = fopen(arg_pidfile, "w");
            if (fh == NULL) {
                logw(logh, LOG_ERR, "Error creating to pidfile %s: %s", arg_pidfile, strerror(errno));
                ret = 1;
            } else {
                if (0 > fprintf(fh, "%d\n", getpid())) {
                    logw(logh, LOG_ERR, "Error writing to pidfile %s: %s", arg_pidfile, strerror(errno));
                    ret = 1;
                }
                fclose(fh);
            }
        }
        if (ret == 0) {
            signal(SIGCHLD, SIG_IGN);
            signal(SIGINT,  react_stop);
            signal(SIGTERM, react_stop);
#ifdef DEBUG
            signal(SIGUSR1, react_debug);
#endif
            ret |= react_main();
            signal(SIGUSR1, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT,  SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
        }
        log_close(logh);
    }

    if (resets)
        expire_list_free(resets, reset_free);
    if (expires)
        expire_list_free(expires, expires_run);

    dprint("freeing memory");
    free_config();

    if (pcre_tables)
        pcre_free((void *)pcre_tables);

    return ret;
}
