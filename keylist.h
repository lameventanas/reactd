#ifndef KEYLIST_H
#define KEYLIST_H

typedef struct _keylist {
	char *key;
	void *value;
	struct _keylist *next;
} keylist;

/*
 * Adds item
 * If key already exists, replaces value (and returns previous one)
 * If key doesn't exist, returns NULL
 * key is strdupped
 */
void *keylist_set(keylist **list, char *key, void *value);

/*
 * Returns value, or NULL if not present
 */
void *keylist_get(keylist **list, char *key);

/*
 * Removes and returns value, or NULL if not present
 * key is freed
 */
void *keylist_del(keylist **list, char *key);


#endif