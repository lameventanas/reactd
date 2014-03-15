/* $Id$ */

#ifndef PCRE_SUBST_H
#define PCRE_SUBST_H

/*
 * Studies a replacement string.
 * Should be used when reusing a replacement string multiple times
 * returns an array of backreferences:
 * - first int: backref number (eg: \2 -> 2)
 * - second int: position in replacement string
 * ends with -1 for position to indicate end of backreferences
 */
int *pcre_subst_study(char *replacement);

/*
 * Replaces a subject already matched with pcre_exec() into a replacement string
 * subject: the string matched with pcre_exec
 * replacement: the replacement string with the backreferences, possibly studied
 * backref: array of backreferences as returned by pcre_subst_study, or NULL
 * ovector: same as returned by pcre_exec()
 * ovecsize: same used with pcre_exec()
 * return: a new allocated string with the substitutions made, must be freed by caller
 */
char *pcre_subst_replace(char *subject, char *replacement, int *backref, int *ovector, int ovecsize, int matches);

#endif
