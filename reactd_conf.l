/*
 * flex input to generate lexical analyzer for configuration file parser
 */

%{
#include "reactd_conf.tab.h"
#include "log.h"

int linenr = 1;
;
%}

%%
[ \t]+     ; // ignore spaces
\n         { ++linenr; } // count lines
@version   { return VERSIONKEY; }
@options   { return OPTIONSKEY; }
pidfile    { return PIDFILEKEY; }
logging    { return LOGGINGKEY; }
logfile    { return LOGFILEKEY; }
logprefix  { return LOGPREFIXKEY; }
loglevel   { return LOGLEVELKEY; }
command    { return COMMANDKEY; }
key        { return KEYKEY; }
trigger    { return TRIGGERKEY; }
reset      { return RESETKEY; }
timeout    { return TIMEOUTKEY; }
in         { return INKEY; }
[0-9]+[ \t]+(second|minute|hour|day)s?    {
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


[0-9]+          { yylval.ival = atoi(yytext); return INT; }
\".+\"          {
    // we have to copy because we can't rely on yytext not changing underneath us:
    yylval.sval = strndup(yytext+1, strlen(yytext)-2);
    printf("strndup: %p %s\n", yylval.sval, yylval.sval);
    return STRING;
}

\. { return DOT; }


[\{\},=]  { return yytext[0]; } // return single characters that are used in the config file
#.*        ; // ignore comments

<<EOF>> {
    printf("EOF of config file\n");
    yy_delete_buffer(YY_CURRENT_BUFFER);
    yyterminate();
}

%%
/*
void yyerror(const char *s) {
    printf("Error parsing line %d: %s\n---\n%s\n---", linenr, s, yytext);
}
*/