/* $Id$ */

#include <stdlib.h>
#include <string.h>
#include <pcre.h>

/*
 * Studies a replacement string.
 * Should be used when reusing a replacement string multiple times
 * returns an array of backreferences:
 * - first int: backref number (eg: \2 -> 2)
 * - second int: position in replacement string
 * ends with -1 for position to indicate end of backreferences
 */
int *pcre_subst_study(char *replacement) {
	int i;
	int bp;
	int *backref;
	
	// worst case we have a full replacement string, eg: "\1\2\3\4\5"
	// we need 2 * number of backref, so this matches strlen(replacement)
	backref = malloc(strlen(replacement) * sizeof(int));
	
	backref[1] = -1;
	bp = 0;
	i = 0;
	while (replacement[i] != '\0') {
		if (replacement[i] == '\\') {
			if (isdigit(replacement[i + 1])) {
				backref[bp+1] = i;
				backref[bp] = 0;
				while (isdigit(replacement[++i])) {
					backref[bp] = backref[bp] * 10 + replacement[i] - 48;
				}
				bp+=2;
				backref[bp] = -1;
				backref[bp+1] = -1;
				i--; // compensate for going one char more than we should
			}
		}
		i++;
	}
	return backref;
}

/*
 * Replaces a subject already matched with pcre_exec() into a replacement string
 * subject: the string matched with pcre_exec
 * replacement: the replacement string with the backreferences, possibly studied
 * backref: array of backreferences as returned by pcre_subst_study, or NULL
 * ovector: same as returned by pcre_exec()
 * ovecsize: same used with pcre_exec()
 * return: a new allocated string with the substitutions made, must be freed by caller
 */
char *pcre_subst_replace(char *subject, char *replacement, int *backref, int *ovector, int ovecsize, int matches) {
	int bp; // backreference position
	int free_backref = 0; // a reminder to free backref, in case caller didn't allocate its memory
	char *ret; // string to return
	
	if (backref == NULL) {
		backref = pcre_subst_study(replacement);
		free_backref = 1;
	}
	
	// calculate length of resulting string
	bp = 0;
	int len = strlen(replacement);
	
	while (backref[bp+1] != -1) {
		// for each backreference substract number of digits and the backslash, and add the matching string length
		len -= (digits(backref[bp]) + 1);
		if (matches > backref[bp] && ovector[2*backref[bp]] != -1) // if there is a match for this backreference, add matched length
			len += (ovector[2*backref[bp]+1] - ovector[2*backref[bp]]);
		bp += 2;
	}
	
	ret = malloc(len + 1);
	ret[len] = 0; // final char
	
	// copy replacement string leading to next backreference
	strncat(ret, replacement, backref[1]);
	
	// substitute backreferences with matches
	for (bp = 0; backref[bp] != -1; bp+=2) {
		if (bp > 0) {
			// copy portion from replacement string leading to this backreference
			strncat(ret, &replacement[backref[bp-2+1] + digits(backref[bp-2])+1 ], backref[bp+1] - (backref[bp-2+1] + digits(backref[bp-2])+1));
		}
		
		// skip the copy from subject if this backreference can't be substituted (not returned by pcre_exec)
		if (matches > backref[bp] && ovector[2*backref[bp]] != -1) {
			// copy captured match from subject
			strncat(ret, &subject[ovector[2*backref[bp]]], ovector[2*backref[bp]+1] - ovector[2*backref[bp]]); 
		}
	}
	
	// if there were any backreferences, copy final portion from replacement string too
	if (backref[1] != -1)
		strcat(ret, &replacement[backref[bp-2+1] + digits(backref[bp-2]) +1 ]);
	
	// if we allocated backreferences, free it
	if (free_backref)
		free(backref);
	
	return ret;
}


/*
 * Return number of digits of a number
 * a more elegant alternative is to use log10(x)+1, but it requires linking with -lm
 */
int digits(int x) {
	int ret = 0;
	
	// not really needed in this case, but:
	if (x < 0) x *= -1;
	if (x == 0)
		return 1;
	while (x > 0) {
		x/= 10;
		ret++;
	}
	return ret;
}
