/* $Id$ */
#include <stdio.h>
#include <string.h>
#include <pcre.h>
#include "pcre_subst.h"

int main(int argc, char *argv[]) {
	if (argc != 4) {
		printf("Syntax: subst_test <string> <re> <replacement>\n");
		exit(1);
	}
	
	const char *error_msg;
	int error_off;
	const unsigned char *pcre_tables = pcre_maketables();
	pcre *re;
	int matches;
#define MAX_RE_CAPTURES 5
	int re_ret[3 * MAX_RE_CAPTURES];
	
	re = pcre_compile(argv[2], 0, &error_msg, &error_off, pcre_tables);
	matches = pcre_exec(re, NULL, argv[1], strlen(argv[1]), 0, 0, re_ret, 3*MAX_RE_CAPTURES);
	printf("pcre_exec returned: %d\n", matches);
	
	if (matches > 0) {
		char *s = pcre_subst_replace(argv[1], argv[3], NULL, re_ret, 3*MAX_RE_CAPTURES, matches);
		printf("->%s<-\n", s);
		free(s);
	} else {
		printf("no match\n");
	}
}