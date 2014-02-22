/*
 * $Id$
 */
#include "keylist.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Adds item
 * If key already exists, replaces value (and returns previous one)
 * If key doesn't exist, returns NULL
 * key is strdupped
 */
void *keylist_set(keylist **list, char *key, void *value) {
	keylist *item;
	keylist *prev;
	void *prev_value;
	int cmp;
	
	prev = NULL;
	item = *list;
	while (item != NULL) {
		cmp = strcmp(item->key, key);
		if (cmp == 0) {
			// item already there, replace value
			prev_value = item->value;
			item->value = value;
			return prev_value;
		}
		if (cmp > 0)
			break;
		prev = item;
		item = item->next;
	}
	// first item in the list
	if (prev == NULL) {
		*list = malloc(sizeof(keylist));
		(*list)->key = strdup(key);
		(*list)->value = value;
		(*list)->next = NULL;
	}
	else {
		// add in the middle or at the end
		prev->next = malloc(sizeof(keylist));
		prev->next->key = strdup(key);
		prev->next->value = value;
		prev->next->next = item;
	}
	return NULL;
}

/*
 * Returns value, or NULL if not present
 */
void *keylist_get(keylist **list, char *key) {
	keylist *item;
	int cmp;
	
	for (item = *list; item != NULL; item = item->next) {
		cmp = strcmp(item->key, key);
		if (cmp == 0)
			return item->value;
		if (cmp > 0)
			break;
	}
	return NULL;
}

/*
 * Removes and returns value, or NULL if not present
 * key is freed
 */
void *keylist_del(keylist **list, char *key) {
	keylist *item;
	keylist *prev;
	void *value;
	int cmp;
	
	prev = NULL;
	item = *list;
	while (item != NULL) {
		cmp = strcmp(item->key, key);
		if (cmp == 0) {
			if (prev == NULL) // first item in the list
				*list = item->next;
			else
				prev->next = item->next;
			value = item->value;
			free(item->key);
			free(item);
			return value;
		}
		if (cmp > 0)
			return NULL;
		prev = item;
		item = item->next;
	}
}
