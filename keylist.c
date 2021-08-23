#include "keylist.h"
#include <string.h>
#include <stdlib.h>

#ifdef DEBUG_KEYLIST
#include <stdio.h>
#endif

void *keylist_set(keylist **list, char *key, void *value) {
#ifdef DEBUG_KEYLIST
    printf("keylist_set key=%s (before):\n", key);
    keylist_print(list, NULL);
#endif
    keylist *item;
    keylist *prev;
    int cmp;

    prev = NULL;
    item = *list;
    while (item != NULL) {
        cmp = strcmp(item->key, key);
        if (cmp == 0) {
            void *ret = item->value;
            item->value = value;
#ifdef DEBUG_KEYLIST
            printf("keylist_set key=%s (after):\n", key);
            keylist_print(list, NULL);
            printf("keylist_set returning %p\n", ret);
#endif
            return ret;
        }
        if (cmp > 0)
            break;
        prev = item;
        item = item->next;
    }
    if (prev == NULL) {
        // will set first in list
        keylist *tmp = *list;
        *list = malloc(sizeof(keylist));
        char *tmp2 = strdup(key);
        (*list)->key = tmp2;
        (*list)->value = value;
        (*list)->next = tmp;
    }
    else {
        prev->next = malloc(sizeof(keylist));
        prev->next->key = strdup(key);
        prev->next->value = value;
        prev->next->next = item;
    }
#ifdef DEBUG_KEYLIST
    printf("keylist_set key=%s (after):\n", key);
    keylist_print(list, NULL);
    printf("keylist_set returning NULL\n");
#endif
    return NULL;
}

void *keylist_get(keylist **list, char *key) {
#ifdef DEBUG_KEYLIST
    printf("keylist_get key=%s\n", key);
    keylist_print(list, NULL);
#endif
    keylist *item;
    int cmp;
    void *ret = NULL;

    for (item = *list; item != NULL; item = item->next) {
        cmp = strcmp(item->key, key);
        if (cmp == 0) {
            ret = item->value;
            break;
        }
        if (cmp > 0)
            break;
    }
#ifdef DEBUG_KEYLIST
    printf("keylist_get returning %p\n", ret);
#endif
    return ret;
}

void keylist_del_match(keylist **list, int (*callback)(void *, void *), void *args) {
#ifdef DEBUG_KEYLIST
    printf("keylist_del_match (before):\n");
    keylist_print(list, NULL);
#endif
    keylist *item = *list;
    keylist *prev = NULL;

    while (item != NULL) {
        if ((*callback)(item->value, args)) {
            if (prev == NULL) { // first item in the list
                *list = item->next;
                keylist *tmp = item;
                item = item->next;
                free(tmp->key);
                free(tmp);
            } else {
                prev->next = item->next;
                free(item->key);
                free(item);
                item = prev->next;
            }
        } else {
            prev = item;
            item = item->next;
        }
    }
#ifdef DEBUG_KEYLIST
    printf("keylist_del_match (after):\n");
    keylist_print(list, NULL);
#endif
}

void keylist_free(keylist **list, void (*callback)(void *)) {
#ifdef DEBUG_KEYLIST
    printf("keylist_free (before):\n");
    keylist_print(list, NULL);
#endif

    keylist *item = *list;
    keylist *next = NULL;

    while (item != NULL) {
        free(item->key);
        if (callback)
            callback(item->value);
        next = item->next;
        free(item);
        item = next;
    }
    *list = NULL;
#ifdef DEBUG_KEYLIST
    printf("keylist_free (after):\n");
    keylist_print(list, NULL);
#endif
}

/*
 * Counts items in keylist
 */
unsigned int keylist_count(keylist **list) {
    unsigned int ret = 0;
    for (keylist *item = *list; item; item = item->next)
        ret++;
    return ret;
}

#ifdef DEBUG_KEYLIST
// print debug information, callback is optional
void keylist_print(keylist **list, void (*callback)(void *)) {
    printf("\tKEYLIST:\n");
    keylist *item = *list;
    unsigned int i = 0;
    while (item != NULL) {
        printf("\t[%u] key:%s value:%p: ", i++, item->key, item->value);
        if (callback)
            (*callback)(item->value);
        else
            puts("");
        item = item->next;
    }
}
#endif