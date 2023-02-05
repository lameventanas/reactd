/*
 * This is processed through m4 to generate a bison parser for config files
 *
 * Change the default characters for m4 quotes and disable comments
 * so that they don't interfere with bison:
 * changequote(「,」) changecom
 */

%{
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "reactd.h"
#include "avl.h"
#include "ring.h"
#include "debug.h"
#include "reactd_conf.tab.h"

extern unsigned char *pcre_tables;
extern FILE *yyin;
extern int yyparse();
extern int yylex();
extern int yylex_destroy();
extern int linenr;

void yyerror(const char *s);
int yydebug = 1;

extern tglobal_cfg cfg; // global config settings, populated by parse_config()

extern tfile **tf;          // tailed file configs, populated by parse_config()
extern unsigned int tf_cnt; // number of items in tf, populated by parse_config()

tcmd *cc  = NULL; // current command being parsed
re *cr    = NULL; // current RE being parsed
tfile *cf = NULL; // current file section config being parsed

// file => tailed file
// used only during config parsing to find pre-existing file config entries
struct avl_table *tf_cfg;
typedef struct {
    char *name;
    tfile *tf;
} tf_cfg_node;

ifdef(「SYSTEMD」,「
extern tjournal **tj;       // tailed journal configs, populated by parse_config()
extern unsigned int tj_cnt; // number of items in tj, populated by parse_config()

tj_filters *cjf = NULL; // current journal filters
tjournal *cj    = NULL; // current journal section config being parsed

// journal's filter => tailed journal
// used only during config parsing to find pre-existing journal config entries
struct avl_table *tj_cfg;
typedef struct {
    tj_filters *filters; // this is a pointer to the filters inside tj
    tjournal *tj;
} tj_cfg_node;
」)

int config_error = 0;

// works in-place (returns s, possibly with different length)
/*
unescape like this:
\\ -> \
\" -> "
\1 -> \1
\n -> NEWLINE
*/
char *unescape(char *s) {
    unsigned int i = 0;
    unsigned int d = 0;
    char escaping = 0;
    while (s[i] != 0) {
        switch (s[i]) {
            case '\\':
                if (escaping) {
                    escaping = 0;
                    s[d++] = s[i];
                } else {
                    escaping = 1;
                }
                break;
            case '"': // consume previous escape character
                if (escaping)
                    escaping = 0;
                s[d++] = s[i];
                break;
            case 'n':
                if (escaping) {
                    escaping = 0;
                    s[d++] = '\n';
                    break;
                }
                // no break here, let it continue to handle as normal "n" if there was no escape before
            default: // don't consume previous escape character
                if (escaping) {
                    escaping = 0;
                    s[d++] = '\\';
                }
                s[d++] = s[i];
                break;
        }
        i++;
    }
    s[d] = 0;
    return s;
}

%}

%define parse.error detailed

%token VERSION_KEY
%token OPTIONS_KEY
%token PIDFILE_KEY
%token LOGDST_KEY
%token LOGFILE_KEY
%token LOGPREFIX_KEY
%token LOGLEVEL_KEY
%token STDERR_KEY
%token STDOUT_KEY
%token FILE_KEY
%token JOURNAL_KEY
%token COMMAND_KEY
%token TIMEOUT_KEY
%token KEY_KEY
%token TRIGGER_KEY
%token RESET_KEY
%token IN_KEY
%token DOT

%union {
    int ival;
    float fval;
    char *sval;
}

%token <ival> TIMEPERIOD
%token <ival> POSITIVE_INT
%token <fval> FLOAT
%token <sval> STRING
%token <sval> REGEX
%token <ival> LOGLEVEL
%token <ival> LOGDST

%%

reactd:
    options body
    | body
    ;

options:
    OPTIONS_KEY '{' option_lines '}'
    ;

option_lines:
    option_lines option_line
    | option_line
    ;

option_line:
    PIDFILE_KEY     '=' STRING   {
                            // use it, unless set already from command-line
                            if (cfg.pidfile == NULL) {
                                unescape($3);
                                cfg.pidfile = $3;
                            } else {
                                free($3);
                            }
                        }
    | LOGDST_KEY    '=' LOGDST   {
                            if (cfg.logdst < 0)
                                cfg.logdst = $3;
                        }
    | LOGFILE_KEY   '=' STRING   {
                            if (cfg.logfile == NULL) {
                                unescape($3);
                                cfg.logfile = $3;
                            } else  {
                                free($3);
                            }
                        }
    | LOGPREFIX_KEY '=' STRING   { unescape($3); cfg.logprefix = $3; }
    | LOGLEVEL_KEY  '=' LOGLEVEL {
                            if (cfg.loglevel < 0)
                                cfg.loglevel  = $3;
                        }
    ;

body:
    log_entries log_entry
    | log_entry
    ;

log_entries:
    log_entries log_entry
    | log_entry
    ;

log_entry:
    file_entry
ifdef(「SYSTEMD」,「
    | journal_entry
」)
    ;

file_entry:
    STRING {
            // we search if this filename was already found before
            // because there could be more than one file section with the same filename
            tf_cfg_node search = {
                .name = $1
            };
            cf = avl_find(tf_cfg, &search);
            if (cf == NULL) {
                dprint("creating new file config entry");
                cf = calloc(1, sizeof(tfile));
                dprint("new cf: %p", cf);
                cf->name = strdup($1);
                dprint("new file name: %p", cf->name);
                // put it in both temporary cfg avl and returned list
                avl_insert(tf_cfg, cf);
                tf = realloc(tf, (tf_cnt+1) * sizeof(tfile *));
                dprint("new tf: %p", tf);
                tf[tf_cnt++] = cf;
            } else {
                dprint("found previous file config entry");
            }
            dprint("starting file section: '%s' item addr: %p", $1, cf);
        }
    '{'
    re_entries
    '}'    {
            cf = NULL;
            dprint("finished file section: '%s'", $1);
            free($1);
        }
    ;

ifdef(「SYSTEMD」,「
journal_entry:
    JOURNAL_KEY {
        dprint("creating new journal filters");
        cjf = calloc(1, sizeof(tj_filters));
        dprint("new cjf: %p", cjf);
    }
    journal_filters
    '{' {
        // we search if this journal filter combination was already used before
        tj_cfg_node search = {
            .filters = cjf
        };
        cj = avl_find(tj_cfg, &search);
        if (cj == NULL) {
            dprint("creating new journal config entry");
            cj = calloc(1, sizeof(tjournal));
            dprint("new cj: %p", cj);
            cj->filters = cjf;
            // put it in both temporary cfg avl and returned list
            avl_insert(tj_cfg, cj);
            tj = realloc(tj, (tj_cnt+1) * sizeof(tjournal *));
            dprint("new tj: %p", tj);
            tj[tj_cnt++] = cj;
        } else {
            dprint("found previous journal config entry with this filter combo");
            // free current journal filter
            // since we will be using the one we had already in the avl
            for (unsigned int i = 0; i < cjf->cnt; i++) {
                free(cjf->fields[i]);
                free(cjf->values[i]);
            }
            free(cjf);
        }

        cjf = NULL;

    }
    re_entries
    '}' {
        dprint("finished journal section");
        cj = NULL;
    }
    ;

journal_filters:
    journal_filters journal_filter
    | journal_filter
    ;

journal_filter:
    STRING '=' STRING {
        unescape($1);
        unescape($3);
        dprint("found journal filter %u: ->%s<- ->%s<-", cjf->cnt, $1, $3);
        // add new field and value to previously allocated tjournal_cfg
        cjf->fields = realloc(cjf->fields, (cjf->cnt+1) * sizeof(char *));
        dprint("new filter fields addr: %p", cjf->fields);
        cjf->values = realloc(cjf->values, (cjf->cnt+1) * sizeof(char *));
        dprint("new filter values addr: %p", cjf->values);
        cjf->fields[cjf->cnt] = $1;
        cjf->values[cjf->cnt] = $3;
        cjf->cnt++;
    }
」)

re_entries:
    re_entries re_entry
    | re_entry
    ;

re_entry:
    REGEX    {
            const char *error_msg;
            int error_off;
            unescape($1);
            cr = calloc(1, sizeof(re));
            dprint("new cr: %p", cr);
            cr->str = $1;
            cr->re = pcre_compile(cr->str, 0, &error_msg, &error_off, pcre_tables);
            if (!cr->re) {
                fprintf(stderr, "error in regular expression in line %d: `%s' at char %d: %s\n", @1.first_line, cr->str, error_off, error_msg);
                config_error = 1;
            } else {
                cr->studied = pcre_study(cr->re, PCRE_STUDY_JIT_COMPILE, &error_msg);
                if (error_msg != NULL) {
                    fprintf(stderr, "error studying regular expression in line %d: `%s': %s\n", @1.first_line, cr->str, error_msg);
                    config_error = 1;
                } else {
                    pcre_fullinfo(cr->re, cr->studied, PCRE_INFO_CAPTURECOUNT, &cr->capture_cnt);
                    cr->capture_cnt++; // add space for \0
                }
            }
            dprint("new re_entry: '%s'  %p", $1, cr);
        }
    '{'
    re_options
    '}'    {
            dprint("finished re section: '%s' %p", $1, $1);

            if (cr->trigger_cnt > 0) {
                if (cr->key == NULL) {
                    fprintf(stderr, "RE `%s' with trigger must have key\n", cr->str);
                    config_error = 1;
                }
                cr->hitlist = avl_create(keyhits_cmp, NULL, NULL);
            }

            // put cr in cf or cj according to what is being parsed
ifdef(「SYSTEMD」,「
            if (cf) {
」)
                dprint("putting re in cf");
                cf->re = realloc(cf->re, (cf->re_cnt+1) * sizeof(re));
                dprint("new re: %p", cf->re);
                cf->re[cf->re_cnt] = cr;
                cf->re_cnt++;
ifdef(「SYSTEMD」,「
                } else {
                dprint("putting re in cj");
                cj->re = realloc(cj->re, (cj->re_cnt+1) * sizeof(re));
                dprint("new re: %p", cj->re);
                cj->re[cj->re_cnt] = cr;
                cj->re_cnt++;
            }
」)
            cr = NULL;
        }
    ;

re_options:
    re_options re_option
    | re_option
    ;

re_option:
    COMMAND_KEY '=' command {
        dprint("re command: assigning previously parsed command %s with %d arguments", cc->args[0], cc->cnt);
        cr->cmd = cc;
        cc = NULL;
    }
    | KEY_KEY '=' STRING    {
                    // dprint("threshold key: '%s'", $3);
                    unescape($3);
                    dprint("re key: ->%s<-", $3);
                    cr->key = pcre_subst_create($3, PCRE_SUBST_DEFAULT);
                    free($3);
                }
    | TRIGGER_KEY '=' POSITIVE_INT IN_KEY TIMEPERIOD    {
                    // dprint("threshold count: %d", $3);
                    // dprint("threshold period: %d", $5);
                    cr->trigger_cnt  = $3;
                    cr->trigger_time = $5;
                    if (cr->trigger_cnt == 0) {
                        fprintf(stderr, "Invalid trigger count: %d\n", cr->trigger_cnt);
                        config_error = 1;
                    }
                    // TIMEPERIOD token already validates against 0
                }
    | RESET_KEY
    '{'    {
            dprint("starting reset section");
        }
    reset_options
    '}'    {
            dprint("finished reset section");
            if (cr->reset_time == 0) {
                fprintf(stderr, "Invalid reset section: missing timeout\n");
                config_error = 1;
            }
            if (cr->reset_cmd == NULL) {
                fprintf(stderr, "Invalid reset section: missing command\n");
                config_error = 1;
            }
        }
    ;

command:
    command command_str
    | command_str
    ;
;

command_str:
    STRING  {
        dprint("new command string: '%s'", $1);
        if (cc == NULL) {
            dprint("new command");
            cc = (tcmd *)calloc(1, sizeof(tcmd));
            dprint("new cc: %p", cc);
        }
        cc->args = (pcre_subst **)realloc(cc->args, sizeof(pcre_subst *) * (cc->cnt+1));
        dprint("adding command argument, new list pointer: %p", cc->args);

        unescape($1);
        cc->args[cc->cnt] = pcre_subst_create($1, PCRE_SUBST_DEFAULT);
        dprint("assigning arg ->%s<- in position %d: %p", $1, cc->cnt, cc->args[cc->cnt]);
#ifdef DEBUG
        {
            char *tmp = pcre_subst_str(cc->args[cc->cnt]);
            dprint("created pcre_subst for arg ->%s<-", tmp);
            free(tmp);
        }
#endif
        free($1);
        cc->cnt ++;
    }
;

reset_options:
    reset_options reset_option
    | reset_option
    ;

reset_option:
    TIMEOUT_KEY '=' TIMEPERIOD    {
                    dprint("reset timeout: %d", $3);
                    cr->reset_time = $3;
                }
    | COMMAND_KEY '=' command {
        dprint("reset command: assigning previously parsed command");
        cr->reset_cmd = cc;
        cc = NULL;
    }
    ;

%%

/*
void yyerror(const char *s) {
    dprint("Parse error: %s", s);
} */

// callback compare for avl tree of filenames
int tf_cfg_cmp(const void *a, const void *b, void *param) {
    return strcmp(((tf_cfg_node *)a)->name, ((tf_cfg_node *)b)->name);
}

ifdef(「SYSTEMD」,「
// callback compare for avl tree of journal log_entries
// we have to make sure all fields and values are the same, but order doesn't matter
int tj_cfg_cmp(const void *a, const void *b, void *param) {
    if (((tj_cfg_node *)a)->filters->cnt != ((tj_cfg_node *)b)->filters->cnt)
        return 1;

    // check that all fields and values match in both
    for (unsigned int i = 0; i < ((tj_cfg_node *)a)->filters->cnt; i++) {
        unsigned char found = 0;
        for (unsigned int j = 0; j < ((tj_cfg_node *)b)->filters->cnt; j++) {
            if (strcmp(((tj_cfg_node *)a)->filters->fields[i], ((tj_cfg_node *)b)->filters->fields[j]) == 0 && strcmp(((tj_cfg_node *)a)->filters->values[i], ((tj_cfg_node *)b)->filters->values[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found)
            return 1;
    }
    return 0;
}
」)

#ifdef DEBUG

// returns command string
// result must be freed
char *cmd_str(tcmd *cmd) {
    char *ret = calloc(1, sizeof(char));

    for (unsigned int ci = 0; ci < cmd->cnt; ci++) {
        char *arg = pcre_subst_str(cmd->args[ci]);
        if (ci > 0) {
            ret = realloc(ret, strlen(ret) + 1 + strlen(arg) + 1);
            strcat(ret, " ");
            strcat(ret, arg);
        } else {
            ret = realloc(ret, strlen(ret) + strlen(arg) + 1);
            strcat(ret, arg);
        }
        free(arg);
    }
    return ret;
}

void print_re(re *re) {
    dprint("\t\t\tre: %p", re);
    dprint("\t\t\tstr: %s", re->str);
    if (re->cmd) {
        char *tmp = cmd_str(re->cmd);
        dprint("\t\t\tcmd: %s", tmp);
        free(tmp);
    }
    if (re->key != NULL) {
        char *tmp = pcre_subst_str(re->key);
        dprint("\t\t\tkey: %s", tmp);
        free(tmp);
    }
    if (re->trigger_cnt > 0)
        dprint("\t\t\ttrigger: %u in %u seconds", re->trigger_cnt, re->trigger_time);
    if (re->reset_time > 0)
        dprint("\t\t\treset_time: %u seconds", re->reset_time);
    if (re->reset_cmd) {
        char *tmp = cmd_str(re->reset_cmd);
        dprint("\t\t\treset_cmd: %s", tmp);
        free(tmp);
    }
    if (re->hitlist)
        dprint("\t\t\thitlist items: %u", avl_count(re->hitlist));
    else
        dprint("\t\t\tno trigger configured");
}

ifdef(「SYSTEMD」,「
// return journal filters as string
// result must be freed
char *tjf_str(tj_filters *tjf) {
    char *ret = calloc(1, sizeof(char));

    for (unsigned int jfi = 0; jfi < tjf->cnt; jfi++) {
        if (jfi > 0) {
            ret = realloc(ret, strlen(ret) + strlen(tjf->fields[jfi]) + strlen(tjf->values[jfi]) + 9);
            strcat(ret, " \"");
            strcat(ret, tjf->fields[jfi]);
            strcat(ret, "\" = \"");
            strcat(ret, tjf->values[jfi]);
            strcat(ret, "\"");
        } else {
            ret = realloc(ret, strlen(ret) + strlen(tjf->fields[jfi]) + strlen(tjf->values[jfi]) + 8);
            strcat(ret, "\"");
            strcat(ret, tjf->fields[jfi]);
            strcat(ret, "\" = \"");
            strcat(ret, tjf->values[jfi]);
            strcat(ret, "\"");
        }
    }

    return ret;
}

void print_tj(tjournal *tj) {
    char *tmp = tjf_str(tj->filters);
    dprint("\t\tfilters: %s", tmp);
    free(tmp);
    for (unsigned int ri = 0; ri < tj->re_cnt; ri++) {
        dprint("\t\tre: %u", ri);
        print_re(tj->re[ri]);
    }
}

」)

void print_tf(tfile *tf) {
    dprint("\tname: %s (%p)", tf->name, tf->name);
    for (unsigned int ri = 0; ri < tf->re_cnt; ri++) {
        dprint("\t\tre: %u", ri);
        print_re(tf->re[ri]);
    }
}

void print_config() {
    dprint("Tailed files: %u", tf_cnt);
    for (unsigned int fi = 0; fi < tf_cnt; fi++) {
        dprint("\ttf: %u (%p)", fi, tf[fi]);
        print_tf(tf[fi]);
    }
ifdef(「SYSTEMD」,「
    for (unsigned int ji = 0; ji < tj_cnt; ji++) {
        dprint("\ttj: %u (%p)", ji, tj[ji]);
        print_tj(tj[ji]);
    }
」)
}
#endif



int parse_config(char *configfile) {
    int ret;
    int i;
    const char *error_msg;
    int error_off;

    dprint("Parsing %s", configfile);

    yyin = fopen(configfile, "r");
    if (yyin == NULL) {
        fprintf(stderr, "Error opening %s: %s\n", configfile, strerror(errno));
        return 1;
    }

    // avl for tailed file config sections
    tf_cfg = avl_create(tf_cfg_cmp, NULL, NULL);

ifdef(「SYSTEMD」,「
    // avl for journal config sections
    tj_cfg = avl_create(tj_cfg_cmp, NULL, NULL);
」)

    ret = yyparse() || config_error;
    fclose(yyin);
    yylex_destroy(); // frees flex buffer stack

#ifdef DEBUG
    print_config();
#endif

    dprint("Parsing finished with code %d", ret);
    dprint("freeing tf_cfg at %p", tf_cfg);
    avl_destroy(tf_cfg, NULL);

ifdef(「SYSTEMD」,「
    dprint("freeing tj_cfg at %p", tj_cfg);
    avl_destroy(tj_cfg, NULL);
」)

    dprint("will return %d", ret);

    return ret;
}
