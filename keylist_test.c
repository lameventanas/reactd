#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "keylist.h"

int main(int argc, char *argv[]) {
	char cmd[50];
	char key[50];
	char value[50];
	int quit = 0;
	keylist *list = NULL;
	char *s;
	keylist *item;
	
	printf("Commands:\nset <key> <value>\nget <key>\ndel <key>\nquit\n");
	while (!quit) {
		printf("Command:\n");
		scanf("%s %s %s\n", cmd, key, value);
		if (!strcmp(cmd, "quit"))
			quit=1;
		if (!strcmp(cmd, "set")) {
			printf("Calling keylist_set key=%s value=%s\n", key, value);
			s = keylist_set(&list, key, strdup(value));
			if (s) {
				printf("Key already existed, previous value: %s\n", s);
				free(s);
			}
		}
		if (!strcmp(cmd, "get")) {
			printf("Calling keylist_get key=%s\n", key);
			s = keylist_get(&list, key);
			printf("Returned value: %s\n", s?s:"NULL");
		}
		if (!strcmp(cmd, "del")) {
			printf("Calling keylist_del key=%s\n", key);
			keylist_del(&list, key);
		}
		printf("--- current list: ---\n");
		for (item = list; item != NULL; item = item->next) {
			printf("key: %s value: %s\n", item->key, item->value);
		}
		printf("---\n");
	}
}
