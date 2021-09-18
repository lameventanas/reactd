/*
 * bison input to generate reactd configuration file parser
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
extern int linenr;

void yyerror(const char *s);
int yydebug = 1;

// parse_config converts cfg_* structures to the ones the program uses
typedef struct {
    char *str;
    char *cmd;
    char *key;
    char *reset_cmd;
    unsigned int trigger_time; // in seconds
    unsigned int reset_time;
    unsigned int trigger_cnt;
} re_cfg; // re config structure

struct avl_table *tf_cfgs; // file => list of REs
typedef struct {
    char *file; // key for avl
    int re_cnt;
    re_cfg *re;
} tfile_cfg;
tfile_cfg *file_cfg = NULL; // current file section config being parsed

// these are the only ones used by the rest of the program:
tfile *tfs;
tglobal_cfg cfg;

%}

%define parse.error verbose

%token VERSIONKEY
%token OPTIONSKEY
%token PIDFILEKEY
%token LOGGINGKEY
%token LOGFILEKEY
%token LOGPREFIXKEY
%token LOGLEVELKEY
%token COMMANDKEY
%token TIMEOUTKEY
%token KEYKEY
%token TRIGGERKEY
%token RESETKEY
%token INKEY
%token DOT

%union {
    int ival;
    float fval;
    char *sval;
}

%token <ival> TIMEPERIOD
%token <ival> INT
%token <fval> FLOAT
%token <sval> STRING

%%

reactd:
    header body
    ;

header:
    version options
    ;

version:
    VERSIONKEY INT DOT INT { cfg.version_major = $2; cfg.version_minor = $2; }
    ;

options:
    OPTIONSKEY '{' option_lines '}'
    ;

option_lines:
    option_lines ',' option_line
    | option_line
    ;

option_line:
    PIDFILEKEY '=' STRING       { cfg.pidfile = $3; }
    | LOGGINGKEY '=' STRING     { cfg.logging = $3; }
    | LOGFILEKEY '=' STRING     { cfg.logfile = $3; }
    | LOGPREFIXKEY '=' STRING   { cfg.logprefix = $3; }
    | LOGLEVELKEY '=' STRING    { cfg.loglevel = $3; }
    ;

body:
    file_entries
    ;

file_entries:
    file_entries file_entry
    | file_entry
    ;

file_entry:
    STRING {
            // we search if this filename was already found before, because there could be more than one file section with the same filename
            tfile_cfg search = {
                .file = $1
            };
            file_cfg = avl_find(tf_cfgs, &search);
            if (file_cfg == NULL) {
                printf("creating new file_cfg\n");
                file_cfg = malloc(sizeof(tfile_cfg));
                printf("alloc: %p\n", file_cfg);
                file_cfg->file = strdup($1);
                file_cfg->re_cnt = 0;
                file_cfg->re = NULL;
                avl_insert(tf_cfgs, file_cfg);
                // keylist_set(&tf_cfgs, $1, file_cfg);
            } else {
                printf("found previous file_cfg\n");
            }
            dprint("starting file section: '%s' item addr: %p", $1, file_cfg);
        }
    '{'
    re_entries
    '}'    {
            file_cfg = NULL;
            dprint("finished file section: '%s'", $1);
            free($1);
        }
    ;

re_entries:
    re_entries re_entry
    | re_entry
    ;

re_entry:
    STRING    {
            dprint("starting re section: '%s'", $1);
            file_cfg->re = realloc(file_cfg->re, sizeof(re_cfg) * (file_cfg->re_cnt + 1));
            printf("realloc of %p -> %p\n", file_cfg->re);
            memset(&file_cfg->re[file_cfg->re_cnt], 0, sizeof(re_cfg));
            file_cfg->re[file_cfg->re_cnt].str = $1;
            /*
            file_cfg->re[file_cfg->re_cnt].cmd = NULL;
            file_cfg->re[file_cfg->re_cnt].reset_cmd = NULL;
            file_cfg->re[file_cfg->re_cnt].trigger_time = 0;
            file_cfg->re[file_cfg->re_cnt].reset_time = 0;
            file_cfg->re[file_cfg->re_cnt].trigger_cnt = 0;
            */
            printf("assigned re_entry %p\n", $1);
        }
    '{'
    re_options
    '}'    {
            dprint("finished re section: '%s' %p", $1, $1);
            file_cfg->re_cnt ++;
        }
    ;

re_options:
    re_options ',' re_option
    | re_option
    ;

re_option:
    COMMANDKEY '=' STRING    {
                    // dprint("re command: '%s'", $3);
                    file_cfg->re[file_cfg->re_cnt].cmd = $3;
                    printf("assigned command %p\n", $3);
                }
    | KEYKEY '=' STRING    {
                    // dprint("threshold key: '%s'", $3);
                    file_cfg->re[file_cfg->re_cnt].key = $3;
                    printf("assigned key %p\n", $3);
                }
    | TRIGGERKEY '=' INT INKEY TIMEPERIOD    {
                    // dprint("threshold count: %d", $3);
                    // dprint("threshold period: %d", $5);
                    file_cfg->re[file_cfg->re_cnt].trigger_cnt = $3;
                    file_cfg->re[file_cfg->re_cnt].trigger_time = $5;
                }
    | RESETKEY
    '{'    {
            dprint("starting reset section");
        }
    reset_options
    '}'    {
            dprint("finished reset section");
        }
    ;

reset_options:
    reset_options ',' reset_option
    | reset_option
    ;

reset_option:
    TIMEOUTKEY '=' TIMEPERIOD    {
                    dprint("reset timeout: %d", $3);
                    file_cfg->re[file_cfg->re_cnt].reset_time = $3;
                }
    | COMMANDKEY '=' STRING    {
                    dprint("reset command: '%s'", $3);
                    file_cfg->re[file_cfg->re_cnt].reset_cmd = $3;
                    printf("assigned reset_cmd %p\n", $3);
                }
    ;

%%

void yyerror(const char *s) {
    printf("Error parsing line %d: %s\n", linenr, s);
}

// callback compare for avl tree
int file_cfg_cmp(const void *a, const void *b, void *param) {
    return strcmp( ((tfile_cfg *)a)->file, ((tfile_cfg *)b)->file);
}
// callback free for avl tree
void file_cfg_free(void *n, void *param) {
//     tfile_cfg *f = n;
//     free(f->file);
//     while (f->re_cnt-- > 0)
//         free(&f->re[f->re_cnt] );
//     free(f);
    printf("free of %p\n", n);
    free(n);
}

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

    tf_cfgs = avl_create(file_cfg_cmp, NULL, NULL);

    ret = yyparse();
    fclose(yyin);
    dprint("Parsing finished with code %d", ret);

    if (ret == 0) {
        unsigned cnt = avl_count(tf_cfgs);
        dprint("file monitor count: %d", cnt);
        tfs = calloc(cnt + 1, sizeof(tfile)); // allocate space for all tfs plus last NULL
        printf("calloc: %p\n", tfs);
        tfile *tf = tfs;
        struct avl_traverser tr;
        for (tfile_cfg *n = avl_t_first(&tr, tf_cfgs); n; n = avl_t_next(&tr)) {
            printf("log file: %s %p\n", n->file, n);
            printf("re count: %d\n", n->re_cnt);
            tf->name = n->file;
            tf->re = calloc(n->re_cnt + 1, sizeof(re)); // allocate space for all RE's plus last NULL
            for (unsigned int i = 0; i < n->re_cnt; i++) {
                printf("re %d %p re: %s\n", i, &n->re[i], n->re[i].str);
                printf("re %d cmd: %s\n", i, n->re[i].cmd);
                printf("re %d key: %s\n", i, n->re[i].key);

                if (n->re[i].key == NULL) {
                    fprintf(stderr, "log file %s re %u: missing key\n", tf->name, i + 1);
                    ret = 1;
                }
                if (n->re[i].cmd == NULL) {
                    fprintf(stderr, "log file %s re %u: missing command\n", tf->name, i + 1);
                    ret = 1;
                }

                tf->re[i].str = n->re[i].str;
                tf->re[i].cmd = n->re[i].cmd;
                tf->re[i].reset_cmd = n->re[i].reset_cmd;
                tf->re[i].trigger_time = n->re[i].trigger_time;
                tf->re[i].reset_time = n->re[i].reset_time;
                tf->re[i].trigger_cnt = n->re[i].trigger_cnt;
                tf->re[i].re = pcre_compile(n->re[i].str, 0, &error_msg, &error_off, pcre_tables);
                if (! tf->re[i].re) {
                    logw(logh, LOG_ERR, "Error in regular expression '%s' at char %d: %s\n", n->re[i].str, error_off, error_msg);
                    ret = 1;
                } else {
                    tf->re[i].re_studied = pcre_study(tf->re[i].re, 0, &error_msg);
                }
                if (n->re[i].key != NULL) {
                    tf->re[i].key = pcre_subst_create(n->re[i].key, PCRE_SUBST_DEFAULT);
                    free(n->re[i].key);
                }
                tf->re[i].hitlist = avl_create(keyhits_cmp, NULL, NULL);
            }
            tf++;
            printf("free %p\n", n->re);
            free(n->re);
        }
        // keylist_free(&tf_cfgs, NULL);
        avl_destroy(tf_cfgs, file_cfg_free);
    }

    printf("will return %d\n", ret);
    return ret;
}
