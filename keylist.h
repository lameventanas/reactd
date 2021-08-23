#ifndef KEYLIST_H
#define KEYLIST_H

typedef struct _keylist {
    char *key;
    void *value;
    struct _keylist *next;
} keylist;

void *keylist_set(keylist **list, char *key, void *value);

void *keylist_get(keylist **list, char *key);

void keylist_del_match(keylist **list, int (*callback)(void *, void *), void *args);

void keylist_free(keylist **list, void (*callback)(void *));

unsigned int keylist_count(keylist **list);

#ifdef DEBUG_KEYLIST

// print debug information, callback is optional
void keylist_print(keylist **list, void (*callback)(void *));
#endif

#endif
