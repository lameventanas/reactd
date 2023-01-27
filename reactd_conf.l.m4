/*
 * This is processed through m4 to generate a flex analyzer to parse config files
 *
 * Change the default characters for m4 quotes and disable comments
 * so that they don't interfere with bison:
 * changequote(「,」) changecom
 */

%{
#include "reactd_conf.tab.h"
#include "log.h"
#include "debug.h"

;
%}
%option noyywrap noinput nounput yylineno

%%
#.*        ; // ignore comments
[ \t\n]+
\.          { return DOT; }
[\{\},=]    { return yytext[0]; } // return single characters that are used in the config file
[1-9][0-9]* { yylval.ival = atoi(yytext); return POSITIVE_INT; }

version    { return VERSION_KEY;   }
options    { return OPTIONS_KEY;   }
pidfile    { return PIDFILE_KEY;   }
logdst     { return LOGDST_KEY;   }
logfile    { return LOGFILE_KEY;   }
logprefix  { return LOGPREFIX_KEY; }
loglevel   { return LOGLEVEL_KEY;  }

(syslog|file|stdout|stderr) {
    yylval.ival = logdst_int(yytext, LOG_TO_SYSLOG);
    return LOGDST;
}

(emergency|alert|critical|error|warning|notice|info|debug) {
    yylval.ival = loglevel_int(yytext, 0);
    return LOGLEVEL;
}

ifdef(「SYSTEMD」,「
journal    { return JOURNAL_KEY; }
」)
command    { return COMMAND_KEY; }
key        { return KEY_KEY;     }
trigger    { return TRIGGER_KEY; }
reset      { return RESET_KEY;   }
timeout    { return TIMEOUT_KEY; }
" in "     { return IN_KEY;      }
[1-9][0-9]*[ \t]+(second|minute|hour|day)s?    {
    int num;
    char unit[7];
    sscanf(yytext, "%d %s", &num, unit);
    if (!strncasecmp(unit, "second", 6)) {
        yylval.ival = num;
    } else if (!strncasecmp(unit, "minute", 6)) {
        yylval.ival = num * 60;
    } else if (!strncasecmp(unit, "hour", 4)) {
        yylval.ival = num * 60 * 60;
    } else {
        yylval.ival = num * 60 * 60 * 24;
    }
    return TIMEPERIOD;
}

\/([^\/]|\\\/)+\/ { // regex
    // we have to copy because we can't rely on yytext not changing underneath us:
    yylval.sval = strndup(yytext+1, strlen(yytext)-2);
    return REGEX;
}

\"([^\"]|\\\")*\" { // quoted string
    // we have to copy because we can't rely on yytext not changing underneath us:
    yylval.sval = strndup(yytext+1, strlen(yytext)-2);
    return STRING;
}

[^ ="\\=\{\}\n]([^ ="\\=\{\}\n]|\\[ ="\\=\{\}])*     { // unquoted string: must not start with special characters occurring in other places, after that they can appear if escaped
    yylval.sval = strdup(yytext);
    return STRING;
}


<<EOF>> {
    dprint("EOF of config file");
    yy_delete_buffer(YY_CURRENT_BUFFER);
    yyterminate();
}

. {
    fprintf(stderr, "Unrecognized token in line %u: ->%s<-\n", yylineno, yytext);
    yyterminate();
}

%%
/*
void yyerror(const char *s) {
    printf("Error parsing line %d: %s\n---\n%s\n---", linenr, s, yytext);
}
*/

