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
@version   { return VERSION_KEY;   }
@options   { return OPTIONS_KEY;   }
pidfile    { return PIDFILE_KEY;   }
logging    { return LOGGING_KEY;   }
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

command    { return COMMAND_KEY; }
key        { return KEY_KEY;     }
trigger    { return TRIGGER_KEY; }
reset      { return RESET_KEY;   }
timeout    { return TIMEOUT_KEY; }
in         { return IN_KEY;      }
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