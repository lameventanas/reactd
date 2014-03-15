/* $Id$ */

#ifndef PCRE_SUBST_H
#define PCRE_SUBST_H

int *pcre_subst_study(char *replacement);
char *pcre_subst_replace(char *subject, char *replacement, int *backref, int *ovector, int ovecsize, int matches);


#endif
