/*
 * bison input to generate reactd configuration file parser
 */

%{
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "reactd.h"
#include "keylist.h"
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

keylist *tf_cfgs = NULL;

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

typedef struct {
    int re_cnt;
    re_cfg *re;
} tfile_cfg; // file monitor config structure

tfile_cfg *tf_cfg = NULL;

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
    STRING    {
            tf_cfg = keylist_get(&tf_cfgs, $1);
            if (tf_cfg == NULL) {
                printf("creating new tf_cfg\n");
                tf_cfg = calloc(1, sizeof(tfile_cfg));
                // TODO: check calloc return
                keylist_set(&tf_cfgs, $1, tf_cfg);
            } else {
                printf("found previous keylist with key\n");
            }
            dprint("starting file section: '%s' keylist item addr: %p", $1, tf_cfg);
            dprint("keylist item count: %d\n", keylist_count(&tf_cfgs));
            free($1);
        }
    '{'
    re_entries
    '}'    {
            tf_cfg = NULL;
            dprint("finished file section: '%s'", $1);
        }
    ;

re_entries:
    re_entries re_entry
    | re_entry
    ;

re_entry:
    STRING    {
            dprint("starting re section: '%s'", $1);
            tf_cfg->re = realloc(tf_cfg->re, sizeof(re_cfg) * (tf_cfg->re_cnt + 1));
            memset(&tf_cfg->re[tf_cfg->re_cnt], 0, sizeof(re_cfg));
            tf_cfg->re[tf_cfg->re_cnt].str = $1;
            /*
            tf_cfg->re[tf_cfg->re_cnt].cmd = NULL;
            tf_cfg->re[tf_cfg->re_cnt].reset_cmd = NULL;
            tf_cfg->re[tf_cfg->re_cnt].trigger_time = 0;
            tf_cfg->re[tf_cfg->re_cnt].reset_time = 0;
            tf_cfg->re[tf_cfg->re_cnt].trigger_cnt = 0;
            */
        }
    '{'
    re_options
    '}'    {
            dprint("finished re section: '%s'", $1);
            tf_cfg->re_cnt ++;
        }
    ;

re_options:
    re_options ',' re_option
    | re_option
    ;

re_option:
    COMMANDKEY '=' STRING    {
                    // dprint("re command: '%s'", $3);
                    tf_cfg->re[tf_cfg->re_cnt].cmd = $3;
                }
    | KEYKEY '=' STRING    {
                    // dprint("threshold key: '%s'", $3);
                    tf_cfg->re[tf_cfg->re_cnt].key = $3;
                }
    | TRIGGERKEY '=' INT INKEY TIMEPERIOD    {
                    // dprint("threshold count: %d", $3);
                    // dprint("threshold period: %d", $5);
                    tf_cfg->re[tf_cfg->re_cnt].trigger_cnt = $3;
                    tf_cfg->re[tf_cfg->re_cnt].trigger_time = $5;
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
                    tf_cfg->re[tf_cfg->re_cnt].reset_time = $3;
                }
    | COMMANDKEY '=' STRING    {
                    dprint("reset command: '%s'", $3);
                    tf_cfg->re[tf_cfg->re_cnt].reset_cmd = $3;
                }
    ;

%%

void yyerror(const char *s) {
    printf("Error parsing line %d: %s\n", linenr, s);
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
    ret = yyparse();
    fclose(yyin);
    dprint("Parsing finished with code %d", ret);

    if (ret == 0) {
        unsigned cnt = keylist_count(&tf_cfgs);
        dprint("file monitor count: %d", cnt);
        tfs = calloc(cnt + 1, sizeof(tfile)); // allocate space for all tfs plus last NULL
        tfile *fmon = tfs;
        keylist *item = tf_cfgs;
        keylist *tmp;
        while (item) {
            printf("log file: %s %p\n", item->key, item->value);
            tfile_cfg *tf_cfg = (tfile_cfg *)item->value;
            printf("re count: %d\n", tf_cfg->re_cnt);
            fmon->name = item->key;
            fmon->re = calloc(tf_cfg->re_cnt + 1, sizeof(re)); // allocate space for all RE's plus last NULL
            for (unsigned int i = 0; i < tf_cfg->re_cnt; i++) {
                printf("re %d %p re: %s\n", i, &tf_cfg->re[i], tf_cfg->re[i].str);
                printf("re %d cmd: %s\n", i, tf_cfg->re[i].cmd);
                printf("re %d key: %s\n", i, tf_cfg->re[i].key);

                if (tf_cfg->re[i].key == NULL) {
                    fprintf(stderr, "log file %s re %u: missing key\n", fmon->name, i + 1);
                    ret = 1;
                }
                if (tf_cfg->re[i].cmd == NULL) {
                    fprintf(stderr, "log file %s re %u: missing command\n", fmon->name, i + 1);
                    ret = 1;
                }

                fmon->re[i].str = tf_cfg->re[i].str;
                fmon->re[i].cmd = tf_cfg->re[i].cmd;
                fmon->re[i].reset_cmd = tf_cfg->re[i].reset_cmd;
                fmon->re[i].trigger_time = tf_cfg->re[i].trigger_time;
                fmon->re[i].reset_time = tf_cfg->re[i].reset_time;
                fmon->re[i].trigger_cnt = tf_cfg->re[i].trigger_cnt;
                fmon->re[i].re = pcre_compile(tf_cfg->re[i].str, 0, &error_msg, &error_off, pcre_tables);
                if (! fmon->re[i].re) {
                    logw(logh, LOG_ERR, "Error in regular expression '%s' at char %d: %s\n", tf_cfg->re[i].str, error_off, error_msg);
                    ret = 1;
                } else {
                    fmon->re[i].re_studied = pcre_study(fmon->re[i].re, 0, &error_msg);
                }
                if (tf_cfg->re[i].key != NULL) {
                    fmon->re[i].key = pcre_subst_create(tf_cfg->re[i].key, PCRE_SUBST_DEFAULT);
                    free(tf_cfg->re[i].key);
                }
            }
            fmon++;
            tmp = item;
            item = item->next;
            free(tf_cfg->re);
            free(tmp->value);
            free(tmp);
        }
        // keylist_free(&tf_cfgs, NULL);
    }

    printf("will return %d\n", ret);
    return ret;
}
