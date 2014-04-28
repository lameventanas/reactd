/*
 * $Id$
 * bison input to generate reactd configuration file parser
 */

%{
#include <stdio.h>
#include "debug.h"
#include "reactd.h"
#include "reactd_conf.tab.h"

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
%token LOGGINGKEY
%token LOGFILEKEY
%token LOGPREFIXKEY
%token LOGLEVELKEY
%token COMMANDKEY
%token THRESHOLDKEY
%token KEYKEY
%token TRIGGERKEY
%token RESETKEY
%token INKEY

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
	VERSIONKEY FLOAT	{ version = $2; }
	;
	
options:
	OPTIONSKEY '{' option_lines '}'
	;
	
option_lines:
	option_lines ',' option_line
	| option_line
	;
	
option_line:
	PIDFILEKEY '=' STRING		{ pidfile = $3; }
	| MAILKEY '=' STRING		{ mail = $3; }
	| LOGGINGKEY '=' STRING		{ logging = $3; }
	| LOGFILEKEY '=' STRING		{ logfile = $3; }
	| LOGPREFIXKEY '=' STRING	{ logprefix = $3; }
	| LOGLEVELKEY '=' STRING	{ loglevel = $3; }
	;

body:
	file_entries
	;
	
file_entries:
	file_entries file_entry
	| file_entry
	;
	
file_entry:
	STRING	{
			dprint("starting file section: '%s' filenum: %d\n", $1, filenum);
			files[filenum].name = $1;
		}
	'{'
	re_entries
	'}'	{
			filenum++;
			dprint("finished file section: '%s' filenum: %d\n", $1, filenum);
		}
	;
	
re_entries:
	re_entries re_entry
	| re_entry
	;
	
re_entry:
	STRING	{
			dprint("starting re section: '%s' renum: %d\n", $1, files[filenum].renum);
			files[filenum].re[files[filenum].renum].str = $1;
		}
	'{'
	re_options
	'}'	{
			dprint("finished re section: '%s'\n", $1);
			files[filenum].renum++;
		}
	;
	
re_options:
	re_options ',' re_option
	| re_option
	;
	
re_option:
	COMMANDKEY '=' STRING	{
					dprint("re command: '%s'\n", $3);
					files[filenum].re[files[filenum].renum].cmd = $3;
				}
	| MAILKEY '=' STRING	{
					dprint("re mail: '%s'\n", $3);
					files[filenum].re[files[filenum].renum].mail = $3;
				}
	| threshold
	|
	;
	
threshold:
	THRESHOLDKEY	{
				dprint("starting threshold section\n");
			}
	'{'
	threshold_options
	'}'		{
				dprint("finished threshold section\n");
			}
	;
	
threshold_options:
	threshold_options ',' threshold_option
	| threshold_option
	;
	
threshold_option:
	KEYKEY '=' STRING	{
					dprint("threshold key: '%s'\n", $3);
					files[filenum].re[files[filenum].renum].threshold.config.key = $3;
				}
	| TRIGGERKEY '=' INT INKEY PERIOD	{
					dprint("threshold count: %d\n", $3);
					dprint("threshold period: %d\n", $5);
					files[filenum].re[files[filenum].renum].threshold.config.trigger_count = $3;
					files[filenum].re[files[filenum].renum].threshold.config.trigger_period = $5;
				}
	| RESETKEY
	'{'	{
			dprint("starting reset section\n");
		}
	reset_options
	'}'	{
			dprint("finished reset section\n");
		}
	;
	
reset_options:
	reset_options ',' reset_option
	| reset_option
	;
	
reset_option:
	TRIGGERKEY '=' INT INKEY PERIOD	{
					dprint("reset count %d\n", $3);
					dprint("reset period %d\n", $5);
					files[filenum].re[files[filenum].renum].threshold.config.reset_count = $3;
					files[filenum].re[files[filenum].renum].threshold.config.reset_period = $5;
				}
	| COMMANDKEY '=' STRING	{
					dprint("reset command: '%s'\n", $3);
					files[filenum].re[files[filenum].renum].threshold.config.reset_cmd = $3;
				}
	;
	
%%

void yyerror(const char *s) {
	printf("Error parsing line %d: %s\n", linenr, s);
}
