/*
 * $Id$
 * bison input to generate reactd configuration file parser
 */

%{
#include <stdio.h>
#include "reactd.h"
#include "reactd_conf.tab.h"

extern FILE *yyin;
extern int yyparse();
extern int yylex();
extern int linenr;

int filenr;
int renr;

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
	STRING { files[filenr].filename = $1; } '{' re_entries '}'	{ printf("finished FILE section: '%s'\n", $1); filenr++; renr=0; }
	;
	
re_entries:
	re_entries re_entry
	| re_entry
	;
	
re_entry:
	STRING { files[filenr].reactions[renr].re_str = $1; } '{' re_options '}'	{ printf("finished re_entry: '%s'\n", $1); renr++; }
	;
	
re_options:
	re_options ',' re_option
	| re_option
	;
	
re_option:
	COMMANDKEY '=' STRING		{ printf("RE command: '%s'\n", $3); files[filenr].reactions[renr].cmd = $3; }
	| MAILKEY '=' STRING		{ printf("RE mail: '%s'\n", $3); files[filenr].reactions[renr].mail = $3; }
	| threshold
	|
	;
	
threshold:
	THRESHOLDKEY {
		if (files[filenr].reactions[renr].threshold != NULL) {
			printf("Only one threshold is allowed per RE\n"); // TODO: bail out here somehow
		} else {
			files[filenr].reactions[renr].threshold = calloc(1, sizeof(tthreshold));
		}
	} '{' threshold_options '}'
	;
	
threshold_options:
	threshold_options ',' threshold_option
	| threshold_option
	;
	
threshold_option:
	KEYKEY '=' STRING			{ printf("Threshold key: '%s'\n", $3); files[filenr].reactions[renr].threshold->key = $3; }
	| COUNTKEY '=' INT			{ printf("Threshold count: %d\n", $3); files[filenr].reactions[renr].threshold->count = $3; }
	| PERIODKEY '=' PERIOD			{ printf("Threshold period: %d\n", $3); files[filenr].reactions[renr].threshold->period = $3; }
	| RESETKEY {
		if (files[filenr].reactions[renr].threshold->reset != NULL) {
			printf("Only one reset is allowed per threshold\n"); // TODO: bail out here somehow
		} else {
			files[filenr].reactions[renr].threshold->reset = calloc(1, sizeof(treset));
		}
	} '{' reset_options '}'
	;
	
reset_options:
	reset_options ',' reset_option
	| reset_option
	;
	
reset_option:
	PERIODKEY '=' PERIOD			{ printf("Reset time %d\n", $3); files[filenr].reactions[renr].threshold->reset->period = $3; }
	| COMMANDKEY '=' STRING			{ printf("Reset command: '%s'\n", $3); files[filenr].reactions[renr].threshold->reset->cmd = $3; }
	;
	
%%

void yyerror(const char *s) {
	printf("Error parsing line %d: %s\n", linenr, s);
}