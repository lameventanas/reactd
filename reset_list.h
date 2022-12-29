#ifndef RESET_LIST_H
#define RESET_LIST_H

#include <time.h>
// reset_list items are sorted ascending by t

typedef struct _treset_item {
    time_t t;
    char *key;
    void *arg;
    struct _treset_item *next;
} treset_item;

typedef struct {
    treset_item *items;
//    time_t next_reset; // -1 when there are no items, this indicates how long can poll sleep (eg: smallest t, t from first item)
} treset_list;

treset_list *reset_list_init();
void reset_list_free();

void reset_list_add(treset_list *list, time_t t, char *key, void *arg);

void reset_list_run(treset_list *list, time_t t, void (*callback)(char *key, void *arg));

// -1 when there are no items, this indicates how long can poll sleep (eg: smallest t, t from first item)
#define reset_list_next_reset(l) (l->items ? l->items->t : -1)

// this is used only for test units, no need to make efficient
unsigned int reset_list_count(treset_list *list);

#ifdef DEBUG_RESET_LIST
void reset_list_print(treset_list *list);
#endif

#endif