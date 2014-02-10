#include "keylist.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// #include <stdlib.h>

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
	
	// printf("keylist_set: setting key %s to value %p\n", key, value);
	prev = NULL;
	item = *list;
	while (item != NULL) {
		// printf("Comparing item key %s with %s\n", item->key, key);
		cmp = strcmp(item->key, key);
		if (cmp == 0) {
			// printf("Found key! returning immediately\n");
			// item already there, replace value
			prev_value = item->value;
			item->value = value;
			return prev_value;
		}
		if (cmp > 0) {
			// printf("Past the key, breaking out\n");
			break;
		}
		prev = item;
		item = item->next;
	}
	// first item in the list
	if (prev == NULL) {
		// printf("Allocating first item in the list\n");
		*list = malloc(sizeof(keylist));
		(*list)->key = strdup(key);
		(*list)->value = value;
		(*list)->next = NULL;
	}
	else {
		// printf("Allocating item in middle or end of list\n");
		// add in the middle or at the end
		prev->next = malloc(sizeof(keylist));
		prev->next->key = strdup(key);
		prev->next->value = value;
		prev->next->next = item;
	}
	return NULL;
	// printf("keylist_set finishing\n");
}

/*
 * Returns value, or NULL if not present
 */
void *keylist_get(keylist **list, char *key) {
	keylist *item;
	int cmp;
	
	// printf("keylist_get looking for key %s\n", key);
	for (item = *list; item != NULL; item = item->next) {
		// printf("Comparing key %s with %s\n", item->key, key);
		cmp = strcmp(item->key, key);
		if (cmp == 0) {
			// printf("Found key! returning immediately\n");
			return item->value;
		}
		if (cmp > 0) {
			// printf("Past the key, breaking out early\n");
			break;
		}
	}
	// printf("keylist_get finishing\n");
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
	
	// printf("keylist_del: deleting key %s\n", key);
	prev = NULL;
	item = *list;
	while (item != NULL) {
		// printf("Comparing item key %s with %s\n", item->key, key);
		cmp = strcmp(item->key, key);
		if (cmp == 0) {
			// printf("Found key! returning immediately\n");
			if (prev == NULL) {
				// first item in the list
				*list = item->next;
			} else {
				prev->next = item->next;
			}
			value = item->value;
			free(item->key);
			free(item);
			return value;
		}
		if (cmp > 0) {
			// printf("Past the key, breaking out\n");
			return NULL;
		}
		prev = item;
		item = item->next;
	}
	// printf("keylist_del finishing\n");
}


