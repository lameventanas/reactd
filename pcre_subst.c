#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <pcre.h>
#include <assert.h>

#ifdef DEBUG
// used only for pcre_subst_str()
#include <math.h>
#include <stdio.h>
#endif

#include "pcre_subst.h"

/*
 * Studies a replacement string.
 * Result is a structure used by pcre_subst_replace()
 * Once a replacement string has been studied, it can be modified or freed.
 */
pcre_subst *pcre_subst_create(char *s, int options) {
    int di; // iterate in tpl
    int i; // iterate in s
    int p; // beginning of previous s substring
    pcre_subst *tpl;

    // TODO: allow null s, just always return empty string?
    assert(s != NULL);

    // printf("pcre_subst_create: studying template %s\n", s);
    // first we figure out how many elements we need for tpl
    di = 0; i = 0; p = 0;
    while (s[i] != '\0') {
        if (s[i] == '\\' && (isdigit(s[i+1]) || !(options | PCRE_SUBST_NO_SPECIAL_CHARS))) {
            if (p < i)
                di++;
            i++;
            if (isdigit(s[i])) {
                while (isdigit(s[i]))
                    i++;
                di++;
            } else {
                if (s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t' || s[i] == '\e' || s[i] == '\a' || s[i] == '\f') {
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
    tpl = malloc((di+1) * sizeof(pcre_subst));
    assert(tpl != NULL);

    // now fill tpl
    di = 0; i = 0; p = 0;
    while (s[i] != '\0') {
        if (s[i] == '\\' && (isdigit(s[i+1]) || !(options | PCRE_SUBST_NO_SPECIAL_CHARS))) {
            // copy previously found s substring
            if (p < i) {
                tpl[di].type = PCRE_SUBST_REPLACEMENT;
                tpl[di].s = strndup(s + p, i - p);
                di++;
            }
            i++;
            if (isdigit(s[i])) {
                tpl[di].num = 0;
                tpl[di].type = PCRE_SUBST_SUBJECT;
                while (isdigit(s[i])) {
                    tpl[di].num = tpl[di].num * 10 + s[i] - '0';
                    i++;
                }
                di++;
            } else {
                tpl[di].type = PCRE_SUBST_REPLACEMENT;
                switch(s[i]) {
                    case '\\': tpl[di].s = strdup("\\"); i++; di++; break;
                    case 'n': tpl[di].s = strdup("\n"); i++; di++; break;
                    case 'r': tpl[di].s = strdup("\r"); i++; di++; break;
                    case 't': tpl[di].s = strdup("\t"); i++; di++; break;
                    case 'e': tpl[di].s = strdup("\e"); i++; di++; break;
                    case 'a': tpl[di].s = strdup("\a"); i++; di++; break;
                    case 'f': tpl[di].s = strdup("\f"); i++; di++; break;
                }
            }
            p = i;
        } else {
            i++;
        }
    }

    // handle last replacement substring
    if (p < i) {
        tpl[di].type = PCRE_SUBST_REPLACEMENT;
        tpl[di].s = strndup(s+p, i-p);
        di++;
    }

    // finalize array
    tpl[di].type = PCRE_SUBST_END;

    // pcre_subst_print(tpl);

    return tpl;
}

/*
 * Replaces a subject already matched with pcre_exec() into a replacement string
 * subject: the string matched with pcre_exec
 * data: structure as returned by pcre_subst_create
 * ovector: same as returned by pcre_exec()
 * ovecsize: same used with pcre_exec()
 * return: a new allocated string with the substitutions made, must be freed by caller
 */
char *pcre_subst_replace(char *subject, pcre_subst *tpl, int *ovector, int ovecsize, int matches, int options) {
    int di = 0;
    int len;
    char *s;
    int i;

    // printf("pcre_subst_replace: replacing '%s' tpl: 0x%X\n", subject, tpl);

    len = 0;
    for (di = 0; tpl[di].type != PCRE_SUBST_END; di++) {
        if (tpl[di].type == PCRE_SUBST_REPLACEMENT) {
            len += strlen(tpl[di].s);
        } else if (tpl[di].type == PCRE_SUBST_SUBJECT) {
            if (matches > tpl[di].num && ovector[2 * tpl[di].num] != -1) {
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    len+=2; // add spaces for quotes around subject substring
                for (i = ovector[2 * tpl[di].num]; i < ovector[2 * tpl[di].num + 1]; i++) {
                    len++;
                    // add extra space for '" and "' that go around single-quote
                    if ((options & PCRE_SUBST_SHELL_ESCAPE_SUBJ) && subject[i] == '\'')
                        len+=3;
                }
            }
        }
    }

    s = malloc(len + 1);
    assert(s != NULL);
    s[0] = '\0';
    s[len] = '\0';

    for (di = 0; tpl[di].type != PCRE_SUBST_END; di++) {
        if (tpl[di].type == PCRE_SUBST_REPLACEMENT) {
            strcat(s, tpl[di].s);
        } else if (tpl[di].type == PCRE_SUBST_SUBJECT) {
            if (matches > tpl[di].num && ovector[2 * tpl[di].num] != -1) {
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    strcat(s, "'"); // start quote of subject substring
                for (i = ovector[2 * tpl[di].num]; i < ovector[2 * tpl[di].num + 1 ]; i++) {
                    if (subject[i] == '\'' && (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ))
                        strcat(s, "'\"'\"'"); // end current single-quote string, add double quote string with ', start new single-quote string
                    else
                        strncat(s, &subject[i], 1);
                }
                if (options & PCRE_SUBST_SHELL_ESCAPE_SUBJ)
                    strcat(s, "'"); // end quote of subject substring
                // strncat(s, &subject[ovector[2 * tpl[di].num]], ovector[2 * tpl[di].num + 1 ] - ovector[2 * tpl[di].num]);
            }
        }
    }

    return s;
}

/*
 * Free replacement data returned by pcre_subst_create()
 */
void pcre_subst_free(pcre_subst *tpl) {
    int di = 0;

    for (di = 0; tpl[di].type != PCRE_SUBST_END; di++)
        if (tpl[di].type == PCRE_SUBST_REPLACEMENT)
            free(tpl[di].s);

    free(tpl);
}

/*
 * Return string representing template (inverse of pcre_subst_create)
 */
#ifdef DEBUG
char *pcre_subst_str(pcre_subst *tpl) {
    int di;
    char *s = calloc(1, sizeof(char));
    unsigned int len = 1;

    for (di = 0; tpl[di].type != PCRE_SUBST_END; di++) {
        if (tpl[di].type == PCRE_SUBST_REPLACEMENT) {
            len += strlen(tpl[di].s);
            s = realloc(s, len*sizeof(char));
            strcat(s, tpl[di].s);
        } else if (tpl[di].type == PCRE_SUBST_SUBJECT) {
            len += 1+((tpl[di].num == 0) ? 1 : 1+(unsigned int)log10(tpl[di].num));
            s = realloc(s, len*sizeof(char));
            sprintf(s + strlen(s), "\\%u", tpl[di].num);
        }
    }
    return s;
}
#endif


/*
void pcre_subst_print(pcre_subst *tpl) {
    int di;

    printf("pcre_subst_print 0x%X\n", tpl);
    for (di = 0; tpl[di].type != PCRE_SUBST_END; di++) {
        printf("%d: ", di);
        if (tpl[di].type == PCRE_SUBST_REPLACEMENT) {
            printf("string: '%s' ", tpl[di].s);
        }
        if (tpl[di].type == PCRE_SUBST_SUBJECT) {
            printf("reference: %d ", tpl[di].num);
        }
        printf("\n");
    }
}
*/
