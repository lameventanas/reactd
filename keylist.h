#ifndef KEYLIST_H
#define KEYLIST_H

typedef struct {
	char *key;
	void *value;
	klist *next;
} keylist;

void keylist_set(keylist **list, char *key, void *value);
void keylist_get(keylist **list, char *key);
void keylist_del(keylist **list, char *key);


#endif