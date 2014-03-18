/* $Id$ */

#include <stdlib.h>
#include <string.h>
#include <pcre.h>
#include "pcre_subst.h"

/*
 * Studies a replacement string.
 * Result is a structure used by pcre_subst_replace()
 * Once a replacement string has been studied, it can be modified or freed.
 */
struct pcre_subst_data *pcre_subst_study(char *replacement) {
	int di; // iterate in data
	int i; // iterate in replacement
	int p; // beginning of previous replacement substring
	struct pcre_subst_data *data;
	
	// first we figure out how many elements we need for data
	di = 0; i = 0; p = 0;
	while (replacement[i] != '\0') {
		if (replacement[i] == '\\') {
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
	data = malloc((di+1) * sizeof(struct pcre_subst_data));

	// now fill data
	di = 0; i = 0; p = 0;
	while (replacement[i] != '\0') {
		if (replacement[i] == '\\') {
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
char *pcre_subst_replace(char *subject, struct pcre_subst_data *data, int *ovector, int ovecsize, int matches) {
	int di = 0;
	int len;
	char *s;
	
	len = 0;
	for (di = 0; data[di].type != PCRE_SUBST_END; di++) {
		if (data[di].type == PCRE_SUBST_REPLACEMENT) {
			len += strlen(data[di].s);
		}
		if (data[di].type == PCRE_SUBST_SUBJECT) {
			if (matches > data[di].num && ovector[2 * data[di].num] != -1)
				len += (ovector[2 * data[di].num + 1] - ovector[2 * data[di].num]);
		}
	}
	s = malloc(len + 1);
	s[len] = '\0';
	
	for (di = 0; data[di].type != PCRE_SUBST_END; di++) {
		if (data[di].type == PCRE_SUBST_REPLACEMENT)
			strcat(s, data[di].s);
		if (data[di].type == PCRE_SUBST_SUBJECT) {
			if (matches > data[di].num && ovector[2 * data[di].num] != -1)
				strncat(s, &subject[ovector[2 * data[di].num]], ovector[2 * data[di].num + 1 ] - ovector[2 * data[di].num]);
		}
	}
	
	return s;
}

/*
 * Free replacement data returned by pcre_subst_study()
 */
void pcre_subst_free(struct pcre_subst_data *data) {
	int di = 0;

	for (di = 0; data[di].type != PCRE_SUBST_END; di++)
		if (data[di].type == PCRE_SUBST_REPLACEMENT)
			free(data[di].s);

	free(data);
}
