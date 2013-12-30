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

/*
extern tfile files[MAXFILES];
extern char *pidfile;
extern char *mail;
extern char *logging;
extern float version;
*/

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
			dprintf("starting file section: '%s' filenr: %d\n", $1, filenr);
			files[filenr].filename = $1;
		}
	'{'
	re_entries
	'}'	{
			dprintf("finished file section: '%s'\n", $1);
			filenr++;
			renr=0;
		}
	;
	
re_entries:
	re_entries re_entry
	| re_entry
	;
	
re_entry:
	STRING	{
			dprintf("starting re section: '%s' renr: %d\n", $1, renr);
			files[filenr].reactions[renr].re_str = $1;
		}
	'{'
	re_options
	'}'	{
			dprintf("finished re section: '%s'\n", $1);
			renr++;
		}
	;
	
re_options:
	re_options ',' re_option
	| re_option
	;
	
re_option:
	COMMANDKEY '=' STRING	{
					dprintf("re command: '%s'\n", $3);
					files[filenr].reactions[renr].cmd = $3;
				}
	| MAILKEY '=' STRING	{
					dprintf("re mail: '%s'\n", $3);
					files[filenr].reactions[renr].mail = $3;
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
					files[filenr].reactions[renr].threshold.key = $3;
				}
	| COUNTKEY '=' INT	{
					dprintf("threshold count: %d\n", $3);
					files[filenr].reactions[renr].threshold.count = $3;
				}
	| PERIODKEY '=' PERIOD	{
					dprintf("threshold period: %d\n", $3);
					files[filenr].reactions[renr].threshold.period = $3;
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
					files[filenr].reactions[renr].threshold.reset.period = $3;
				}
	| COMMANDKEY '=' STRING	{
					dprintf("reset command: '%s'\n", $3);
					files[filenr].reactions[renr].threshold.reset.cmd = $3;
				}
	;
	
%%

void yyerror(const char *s) {
	printf("Error parsing line %d: %s\n", linenr, s);
}