/*
 * $Id$
 * bison input to generate reactd configuration file parser
 */

%{
#include <stdio.h>
#include "reactd.tab.h"

extern FILE *yyin;
extern int yyparse();
extern int yylex();
extern int linenr;
void yyerror(const char *s);

int yydebug=1;

%}
%define parse.error verbose

%token VERSIONKEY
%token OPTIONSKEY
%token PIDFILEKEY
%token MAILKEY
%token LOGKEY
%token COMMANDKEY
%token THRESHOLDKEY
%token KEYKEY
%token COUNTKEY
%token PERIODKEY
%token RESETKEY

%union {
	int ival;
	float fval;
	char *sval;
}

%token <ival> PERIOD
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
	VERSIONKEY FLOAT	{ printf("Version: %f\n", $2); }
	;
	
options:
	OPTIONSKEY '{' option_lines '}'
	;
	
option_lines:
	option_lines ',' option_line
	| option_line
	;
	
option_line:
	PIDFILEKEY '=' STRING			{ printf("Option pidfile: '%s'\n", $3); }
	| MAILKEY '=' STRING			{ printf("Option mail: '%s'\n", $3); }
	| LOGKEY '=' STRING	 		{ printf("Option log: '%s'\n", $3); }
	;
	
body:
	file_entries
	;
	
file_entries:
	file_entries file_entry
	| file_entry
	;
	
file_entry:
	STRING '{' re_entries '}'	{ printf("finished FILE section: '%s'\n", $1); }
	;
	
re_entries:
	re_entries re_entry
	| re_entry
	;
	
re_entry:
	STRING '{' re_options '}'	{ printf("finished re_entry: '%s'\n", $1); }
	;
	
re_options:
	re_options ',' re_option
	| re_option
	;
	
re_option:
	COMMANDKEY '=' STRING		{ printf("RE command: '%s'\n", $3); }
	| MAILKEY '=' STRING		{ printf("RE mail: '%s'\n", $3); }
	| threshold
	|
	;
	
threshold:
	THRESHOLDKEY '{' threshold_options '}'
	;
	
threshold_options:
	threshold_options ',' threshold_option
	| threshold_option
	;
	
threshold_option:
	KEYKEY '=' STRING			{ printf("Threshold key: '%s'\n", $3); }
	| COUNTKEY '=' INT			{ printf("Threshold count: %d\n", $3); }
	| PERIODKEY '=' PERIOD			{ printf("Threshold period: %d\n", $3); }
	| RESETKEY '{' reset_options '}'
	;
	
reset_options:
	reset_options ',' reset_option
	| reset_option
	;
	
reset_option:
	PERIODKEY '=' PERIOD			{ printf("Reset time %d\n", $3); }
	| COMMANDKEY '=' STRING			{ printf("Reset command: '%s'\n", $3); }
	;
	
%%

#include <errno.h>

int main(int argc, char *argv[]) {
	FILE *file;
	if (argc != 2) {
		printf("Syntax: reactd <config file>\n");
		return(1);
	}
	printf("Will parse '%s'\n", argv[1]);
	file = fopen(argv[1], "r");
	if (file == NULL) {
		printf("Could not open '%s': %s\n", argv[1], strerror(errno));
		return(1);
	}
	yyin = file;
	do {
		yyparse();
	} while (!feof(file));
	fclose(file);
	return(0);
}

void yyerror(const char *s) {
	printf("Error parsing line %d: %s\n", linenr, s);
}