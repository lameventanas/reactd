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

    int options = PCRE_SUBST_DEFAULT;
    options |= PCRE_SUBST_NO_SPECIAL_CHARS;
    options |= PCRE_SUBST_SHELL_ESCAPE_SUBJ;

    re = pcre_compile(argv[2], 0, &error_msg, &error_off, pcre_tables);

    pcre_subst_data *subst_data = pcre_subst_study(argv[3], options);

    matches = pcre_exec(re, NULL, argv[1], strlen(argv[1]), 0, 0, re_ret, 3*MAX_RE_CAPTURES);
    printf("pcre_exec returned: %d\n", matches);

    if (matches > 0) {
        char *s = pcre_subst_replace(argv[1], subst_data, re_ret, 3*MAX_RE_CAPTURES, matches, options);
        printf("->%s<-\n", s);
        pcre_subst_free(subst_data);
        free(s);
    } else {
        printf("no match\n");
    }
}