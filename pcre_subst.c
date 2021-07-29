#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <pcre.h>
#include "pcre_subst.h"

/*
 * Studies a replacement string.
 * Result is a structure used by pcre_subst_replace()
 * Once a replacement string has been studied, it can be modified or freed.
 */
pcre_subst_data *pcre_subst_study(char *replacement, int options) {
    int di; // iterate in data
    int i; // iterate in replacement
    int p; // beginning of previous replacement substring
    pcre_subst_data *data;

    // printf("pcre_subst_study: studying %s\n", replacement);
    // first we figure out how many elements we need for data
    di = 0; i = 0; p = 0;
    while (replacement[i] != '\0') {
        if (replacement[i] == '\\' && (isdigit(replacement[i+1]) || !(options | PCRE_SUBST_NO_SPECIAL_CHARS))) {
            if (p < i)
                di++;
            i++;
            if (isdigit(replacement[i])) {
                while (isdigit(replacement[i]))
                    i++;
                di++;
            } else {
                if (replacement[i] == '\\' || replacement[i] == '\n' || replacement[i] == '\r' || replacement[i] == '\t' || replacement[i] == '\e' || replacement[i] == '\a' || replacement[i] == '\f') {
                    i++;
                    di++;
                }
            }
            p = i;
        } else {
            i++;
        }
    }
    // last replacement substring
    if (p < i)
        di++;

    // allocate space for each element plus the PCRE_SUBST_END one
    data = malloc((di+1) * sizeof(pcre_subst_data));

    // now fill data
    di = 0; i = 0; p = 0;
    while (replacement[i] != '\0') {
        if (replacement[i] == '\\' && (isdigit(replacement[i+1]) || !(options | PCRE_SUBST_NO_SPECIAL_CHARS))) {
            // copy previously found replacement substring
            if (p < i) {
                data[di].type = PCRE_SUBST_REPLACEMENT;
                data[di].s = strndup(replacement + p, i - p);
                di++;
            }
            i++;
            if (isdigit(replacement[i])) {
                data[di].type = PCRE_SUBST_SUBJECT;
                while (isdigit(replacement[i])) {
                    data[di].num = data[di].num * 10 + replacement[i] - '0';
                    i++;
                }
                di++;
            } else {
                data[di].type = PCRE_SUBST_REPLACEMENT;
                switch(replacement[i]) {
                    case '\\': data[di].s = strdup("\\"); i++; di++; break;
                    case 'n': data[di].s = strdup("\n"); i++; di++; break;
                    case 'r': data[di].s = strdup("\r"); i++; di++; break;
                    case 't': data[di].s = strdup("\t"); i++; di++; break;
                    case 'e': data[di].s = strdup("\e"); i++; di++; break;
                    case 'a': data[di].s = strdup("\a"); i++; di++; break;
                    case 'f': data[di].s = strdup("\f"); i++; di++; break;
                }
            }
            p = i;
        } else {
            i++;
        }
    }

    // handle last replacement substring
    if (p < i) {
        data[di].type = PCRE_SUBST_REPLACEMENT;
        data[di].s = strndup(replacement+p, i-p);
        di++;
    }

    // finalize array
    data[di].type = PCRE_SUBST_END;

    // pcre_subst_print(data);

    return data;

}

/*
 * Replaces a subject already matched with pcre_exec() into a replacement string
 * subject: the string matched with pcre_exec
 * data: structure as returned by pcre_subst_study
 * ovector: same as returned by pcre_exec()
 * ovecsize: same used with pcre_exec()
 * return: a new allocated string with the substitutions made, must be freed by caller
 */
char *pcre_subst_replace(char *subject, pcre_subst_data *data, int *ovector, int ovecsize, int matches, int options) {
    int di = 0;
    int len;
    char *s;
    int i;

    // printf("pcre_subst_replace: replacing '%s' data: 0x%X\n", subject, data);

    len = 0;
    for (di = 0; data[di].type != PCRE_SUBST_END; di++) {
        if (data[di].type == PCRE_SUBST_REPLACEMENT) {
            len += strlen(data[di].s);
        } else if (data[di].type == PCRE_SUBST_SUBJECT) {
            if (matches > data[di].num && ovector[2 * data[di].num] != -1) {
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    len+=2; // add spaces for quotes around subject substring
                for (i = ovector[2 * data[di].num]; i < ovector[2 * data[di].num + 1]; i++) {
                    len++;
                    // add extra space for '" and "' that go around single-quote
                    if ((options & PCRE_SUBST_SHELL_ESCAPE_SUBJ) && subject[i] == '\'')
                        len+=3;
                }
            }
        }
    }

    s = malloc(len + 1);
    s[0] = '\0';
    s[len] = '\0';

    for (di = 0; data[di].type != PCRE_SUBST_END; di++) {
        if (data[di].type == PCRE_SUBST_REPLACEMENT) {
            strcat(s, data[di].s);
        } else if (data[di].type == PCRE_SUBST_SUBJECT) {
            if (matches > data[di].num && ovector[2 * data[di].num] != -1) {
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    strcat(s, "'"); // start quote of subject substring
                for (i = ovector[2 * data[di].num]; i < ovector[2 * data[di].num + 1 ]; i++) {
                    if (subject[i] == '\'' && (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ))
                        strcat(s, "'\"'\"'"); // end current single-quote string, add double quote string with ', start new single-quote string
                    else
                        strncat(s, &subject[i], 1);
                }
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    strcat(s, "'"); // end quote of subject substring
                // strncat(s, &subject[ovector[2 * data[di].num]], ovector[2 * data[di].num + 1 ] - ovector[2 * data[di].num]);
            }
        }
    }

    return s;
}

/*
 * Free replacement data returned by pcre_subst_study()
 */
void pcre_subst_free(pcre_subst_data *data) {
    int di = 0;

    for (di = 0; data[di].type != PCRE_SUBST_END; di++)
        if (data[di].type == PCRE_SUBST_REPLACEMENT)
            free(data[di].s);

    free(data);
}

/*
void pcre_subst_print(pcre_subst_data *data) {
    int di;

    printf("pcre_subst_print 0x%X\n", data);
    for (di = 0; data[di].type != PCRE_SUBST_END; di++) {
        printf("%d: ", di);
        if (data[di].type == PCRE_SUBST_REPLACEMENT) {
            printf("string: '%s' ", data[di].s);
        }
        if (data[di].type == PCRE_SUBST_SUBJECT) {
            printf("reference: %d ", data[di].num);
        }
        printf("\n");
    }
}
*/