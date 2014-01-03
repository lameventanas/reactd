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
	PIDFILEKEY '=' STRING	{ pidfile = $3; }
	| MAILKEY '=' STRING	{ mail = $3; }
	| LOGKEY '=' STRING	{ logging = $3; }
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
			dprintf("starting file section: '%s' filenum: %d\n", $1, filenum);
			files[filenum].name = $1;
		}
	'{'
	re_entries
	'}'	{
			filenum++;
			dprintf("finished file section: '%s' filenum: %d\n", $1, filenum);
		}
	;
	
re_entries:
	re_entries re_entry
	| re_entry
	;
	
re_entry:
	STRING	{
			dprintf("starting re section: '%s' renum: %d\n", $1, files[filenum].renum);
			files[filenum].reactions[files[filenum].renum].str = $1;
		}
	'{'
	re_options
	'}'	{
			dprintf("finished re section: '%s'\n", $1);
			files[filenum].renum++;
		}
	;
	
re_options:
	re_options ',' re_option
	| re_option
	;
	
re_option:
	COMMANDKEY '=' STRING	{
					dprintf("re command: '%s'\n", $3);
					files[filenum].reactions[files[filenum].renum].cmd = $3;
				}
	| MAILKEY '=' STRING	{
					dprintf("re mail: '%s'\n", $3);
					files[filenum].reactions[files[filenum].renum].mail = $3;
				}
	| threshold
	|
	;
	
threshold:
	THRESHOLDKEY	{
				dprintf("starting threshold section\n");
			}
	'{'
	threshold_options
	'}'		{
				dprintf("finished threshold section\n");
			}
	;
	
threshold_options:
	threshold_options ',' threshold_option
	| threshold_option
	;
	
threshold_option:
	KEYKEY '=' STRING	{
					dprintf("threshold key: '%s'\n", $3);
					files[filenum].reactions[files[filenum].renum].threshold.key = $3;
				}
	| COUNTKEY '=' INT	{
					dprintf("threshold count: %d\n", $3);
					files[filenum].reactions[files[filenum].renum].threshold.count = $3;
				}
	| PERIODKEY '=' PERIOD	{
					dprintf("threshold period: %d\n", $3);
					files[filenum].reactions[files[filenum].renum].threshold.period = $3;
				}
	| RESETKEY
	'{'	{
			dprintf("starting reset section\n");
		}
	reset_options
	'}'	{
			dprintf("finished reset section\n");
		}
	;
	
reset_options:
	reset_options ',' reset_option
	| reset_option
	;
	
reset_option:
	PERIODKEY '=' PERIOD	{
					dprintf("reset time %d\n", $3);
					files[filenum].reactions[files[filenum].renum].threshold.reset.period = $3;
				}
	| COMMANDKEY '=' STRING	{
					dprintf("reset command: '%s'\n", $3);
					files[filenum].reactions[files[filenum].renum].threshold.reset.cmd = $3;
				}
	;
	
%%

void yyerror(const char *s) {
	printf("Error parsing line %d: %s\n", linenr, s);
}
