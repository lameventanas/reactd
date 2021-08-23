#ifndef PCRE_SUBST_H
#define PCRE_SUBST_H

// pcre_subst_study returns a null-terminated array of pcre_subst
#define PCRE_SUBST_END 0
#define PCRE_SUBST_REPLACEMENT 1
#define PCRE_SUBST_SUBJECT 2
typedef struct {
    int type;
    char *s; // string from replacement, when type = PCRE_SUBST_REPLACEMENT
    int num; // number of backreference, when type = PCRE_SUBST_SUBJECT
} pcre_subst;


// default: substitutes \\ \n \r \t \e \a \f in replacement substrings and does not quote subject substrings
#define PCRE_SUBST_DEFAULT 0

// do not substitute \\ \n \r \t \e \a \f in replacement substrings (applies to study function only)
#define PCRE_SUBST_NO_SPECIAL_CHARS 1

// single-quote subject substrings, and escape any found single-quotes in them (applies to replace function only)
#define PCRE_SUBST_SHELL_ESCAPE_SUBJ 2

/*
 * Create a subst structure.
 * Result can be used repeatedly by pcre_subst_replace()
 * Once a replacement string has been studied, it can be modified or freed.
 */
pcre_subst *pcre_subst_create(char *s, int options);

/*
 * Replaces matches returned by pcre_exec() into pcre_subst structure, returning a string
 * subject: the string matched with pcre_exec
 * subst: structure as returned by pcre_subst_study
 * ovector: same as returned by pcre_exec()
 * ovecsize: same used with pcre_exec()
 * return: a new allocated string with the substitutions made, must be freed by caller
 */
char *pcre_subst_replace(char *subject, pcre_subst *subst, int *ovector, int ovecsize, int matches, int options);

/*
 * Free replacement data returned by pcre_subst_study()
 */
void pcre_subst_free(pcre_subst *data);

#endif
