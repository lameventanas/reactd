/*
 * $Id: $
 * flex input to generate lexical analyzer for configuration file parser
 */

%{
#include "reactd.tab.h"

int linenr = 1;

%}

%%
[ \t]+		{ printf("flex found spaces\n"); }
\n		{ printf("flex found a newline\n"); ++linenr; }
@version	{ printf("flex will return version key\n"); return VERSIONKEY; }
@options	{ printf("flex will return options key\n"); return OPTIONSKEY; }
pidfile		{ printf("flex will return pidfile key\n"); return PIDFILEKEY; }
mail		{ printf("flex will return mail key\n"); return MAILKEY; }
log		{ printf("flex will return log key\n"); return LOGKEY; }
command		{ printf("flex will return command key\n"); return COMMANDKEY; }
threshold	{ printf("flex will return threshold key\n"); return THRESHOLDKEY; }
key		{ printf("flex will return key key\n"); return KEYKEY; }
count		{ printf("flex will return count key\n"); return COUNTKEY; }
period		{ printf("flex will return period key\n"); return PERIODKEY; }
reset		{ printf("flex will return reset key\n"); return RESETKEY; }
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
	printf("flex will return a time period of %d seconds (%s)\n", yylval.ival, yytext);
	return PERIOD;
}
[0-9]+\.[0-9]+	{ printf("flex will return a float: %s\n", yytext); yylval.fval = atof(yytext); return FLOAT; }
[0-9]+		{ printf("flex will return an integer: %s\n", yytext); yylval.ival = atoi(yytext); return INT; }
\".*\"	{
		// we have to copy because we can't rely on yytext not changing underneath us:
		yylval.sval = strdup(yytext);
		printf("flex will return quoted string: %s\n", yytext);
		return STRING;
		}
[\{\},=]		{ printf("flex will return a single character: %s\n", yytext); return yytext[0]; }
.		{ printf("flex will discard this: %s\n", yytext); }
#.*		{ printf("flex will discard this as a comment: %s\n", yytext); }

%%