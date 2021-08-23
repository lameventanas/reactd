#include <stdlib.h>
#include "reset_list.h"

#ifdef DEBUG_RESET_LIST
#include <stdio.h>
#define dprintf printf
#else
#define dprintf
#endif

treset_list *reset_list_init() {
    treset_list *list = malloc(sizeof(treset_list));
    if (list) {
        list->items = NULL;
        // list->next_reset = -1;
    }
    return list;
}

void reset_list_free(treset_list *list,  void (*callback)(time_t t, char *key, char *cmd)) {
    treset_item *item = list->items;
    while (item) {
        if (callback)
            (callback)(item->t, item->key, item->cmd);
        treset_item *tmp = item;
        item = item->next;
        free(tmp);
    }
    free(list);
}

void reset_list_add(treset_list *list, time_t t, char *key, char *cmd) {
    // if (t < list->next_reset)
        //list->next_reset = t;
    treset_item *item = malloc(sizeof(treset_item));
    item->t = t;
    item->key = key;
    item->cmd = cmd;
    item->next = NULL;

    dprintf("reset_list_add\n");
    treset_item *prev = NULL;
    for (treset_item *o = list->items; o; o = o->next) {
        dprintf("item t=%d key=%s\n", o->t, o->key);
        if (o->t > item->t) {
            dprintf("passed position for new item\n");
            // found position to insert new item
            item->next = o->next;
            o->next = item;
            return;
        }
        prev = o;
    }
    if (prev) {
        // add after last
        dprintf("adding after last\n");
        prev->next = item;
    } else {
        // add as first item
        dprintf("adding as first\n");
        list->items = item;
    }
}

void reset_list_run(treset_list *list, time_t t, void (*callback)(char *key, char *cmd)) {
    while (list->items != NULL && t >= list->items->t) {
        callback(list->items->key, list->items->cmd);
        treset_item *tmp = list->items;
        list->items = list->items->next;
        free(tmp);
    }
}

unsigned int reset_list_count(treset_list *list) {
    unsigned int ret = 0;
    for (treset_item *o = list->items; o != NULL; o = o->next) {
        ret ++;
    }
    return ret;
}

#ifdef DEBUG_RESET_LIST
void reset_list_print(treset_list *list) {
    if (list == NULL) {
        dprintf("reset_list is null\n");
        return;
    }
    unsigned int i = 0;
    for (treset_item *o = list->items; o; o = o->next) {
        dprintf("%u: %p t=%d key=%s next=%p\n", i++, o, o->t, o->key, o->next);
    }
    dprintf("reset_list item count: %u\n", i);
}
#endif