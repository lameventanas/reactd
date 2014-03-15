/* $Id$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>

/* returns an array of backreferences
 * first int: backref number (eg: \2 -> 2)
 * second int: position in replacement string
 * ends with -1 for position to indicate end of backreferences
 */
int *pcre_subst_study(char *replacement) {
	int i;
	int bp;
	int *backref; // first int: backref number. second int: position. etc... ends with -1 for num and position
	// scan replacement string for backref
	
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
				//printf("new backref in pos %d: ", i);
				backref[bp] = 0;
				while (isdigit(replacement[++i])) {
					backref[bp] = backref[bp] * 10 + replacement[i] - 48;
				}
				//printf("%d\n", backref[bp]);
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
 * replacement: the replacement string with the back-references
 * subject: the string matched with pcre_exec
 * ovector: same as pcre_exec
 * ovecsize: same as pcre_exec
 * return: a new allocated string with the substitutions made
 */

/* ovector:
 * 0: points to first char of whole match
 * 1: points to last char of whole match
 * 2: points to first char of 1st subpattern
 * 3: points to last char of 1st subpattern
 * ...
 * notes:
 * - the last 1/3 of ovector is not used
 * - a negative value means nothing was captured, but the next subpattern could have captured something
 */


/*
 * re:
 * soy (\w+)\s+(\w+)
 * 
 * subject:
 * soy alan brito
 * 
 * replacement:
 * hola \1 \2 que tal?
 * 
 * backref:
 * 1, 5, 2, 8
 * 
 * ovector:
 * 0, 14, 4, 8, 9, 14
 * 
 */

char *pcre_subst_replace(char *subject, char *replacement, int *backref, int *ovector, int ovecsize, int matches) {
	int bp; // backreference position
	int free_backref = 0; // a reminder to free backref, in case caller didn't allocate its memory
	
	printf("replacement: '%s'\nsubject: '%s'\n", replacement, subject);
	
	if (backref == NULL) {
		backref = pcre_subst_study(replacement);
		free_backref = 1;
	}
	
	bp = 0;
	int len = strlen(replacement);
	//printf("length of replacement string: %d\n", len);
	while (backref[bp+1] != -1) {
		len = len - (digits(backref[bp]) + 1) + (ovector[2*backref[bp]+1] - ovector[2*backref[bp]]);
		//printf("length of \\%d: %d\n", backref[bp], (digits(backref[bp]) + 1));
		//printf("length of captured string %d: %d\n", backref[bp], ovector[2*backref[bp]+1] - ovector[2*backref[bp]]);
		bp += 2;
	}
	//printf("length of new string: %d\n", len);
	
	//char *ret = malloc(len + 1);
	//ret[len] = 0;
	char *ret = calloc(1, len + 1);
	
	strncat(ret, replacement, backref[1]); // copy replacement string leading to next backreference
	
	//printf("before replacements: '%s'\n", ret);
	
	for (bp = 0; backref[bp] != -1; bp+=2) {
		//printf("new back reference, ret: '%s'\n", ret);
		//printf("ovector for \\%d: %d %d\n", backref[bp], ovector[2*backref[bp]], ovector[2*backref[bp]+1]);
		
		if (bp > 0) {
			//printf("copying portion from replacement before this backreference\n");
			//printf("end of previous replacement: %d\n", backref[bp-2+1] + digits(backref[bp-2])+1);
			//printf("start of this new replacement: %d\n", backref[bp+1]);
			
			strncat(ret, &replacement[backref[bp-2+1] + digits(backref[bp-2])+1 ], backref[bp+1] - (backref[bp-2+1] + digits(backref[bp-2])+1)); // copy replacement string leading to next backreference

			// printf("copied, ret: '%s'\n", ret);
		}
		
		// skip the copy from subject if this backreference can't be substituted (not returned by pcre_exec)
		if (matches > backref[bp] && ovector[2*backref[bp]] != -1) {
			// printf("copying from subject in position: %d count: %d\n", ovector[2*backref[bp]], ovector[2*backref[bp]+1] - ovector[2*backref[bp]]);
			strncat(ret, &subject[ovector[2*backref[bp]]], ovector[2*backref[bp]+1] - ovector[2*backref[bp]]); // copy captured match from subject
			// printf("copied, ret: '%s'\n", ret);
		}
	}
	if (backref[1] != -1) { // if there were any backreferences
		// printf("copying final portion from replacement in positon: %d\n", backref[bp-2+1] + digits(backref[bp-2]) +1 );
		strcat(ret, &replacement[backref[bp-2+1] + digits(backref[bp-2]) +1 ]);
	}
//	printf("ret: '%s'\n", ret);
	if (free_backref)
		free(backref);
	return ret;
}


// a more elegant alternative is to use log10(x)+1, but it requires linking with -lm
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


