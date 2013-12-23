/*
 * $Id$
 * flex input to generate lexical analyzer for configuration file parser
 */

%{
#include "reactd.tab.h"

int linenr = 1;

%}

%%
[ \t]+		; // ignore spaces
\n		{ ++linenr; } // count lines
@version	{ return VERSIONKEY; }
@options	{ return OPTIONSKEY; }
pidfile		{ return PIDFILEKEY; }
mail		{ return MAILKEY; }
log		{ return LOGKEY; }
command		{ return COMMANDKEY; }
threshold	{ return THRESHOLDKEY; }
key		{ return KEYKEY; }
count		{ return COUNTKEY; }
period		{ return PERIODKEY; }
reset		{ return RESETKEY; }
[0-9]+[ \t]+(second|minute|hour|day)s?	{
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
	return PERIOD;
}
[0-9]+\.[0-9]+	{ yylval.fval = atof(yytext); return FLOAT; }
[0-9]+		{ yylval.ival = atoi(yytext); return INT; }
\".*\"		{
	// we have to copy because we can't rely on yytext not changing underneath us:
	yylval.sval = strndup(yytext+1, strlen(yytext)-2);
	return STRING;
}
[\{\},=]	{ return yytext[0]; } // return single characters that are used in the config file
#.*		; // ignore comments

%%