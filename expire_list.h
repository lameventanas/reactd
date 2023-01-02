#ifndef EXPIRE_LIST_H
#define EXPIRE_LIST_H

#include <time.h>
// expire_list items are sorted ascending by t (first to expire goes first)

// TODO: add comparison function to the structure, for expire_list_update()?
// right now I'm finding items by the address

typedef struct _texpire_item {
    time_t t;
    void *obj;
    struct _texpire_item *next;
} texpire_item;

typedef struct {
    texpire_item *items;
    int (*cmp)(const void *, const void *); // comparison function between items, used for updates
} texpire_list;

texpire_list *expire_list_init(int (*)(const void *, const void *));

void expire_list_free(texpire_list *list,  void (*callback_free)(void *obj));

// add item
// NOTE: doesn't check if it already exists
void expire_list_add(texpire_list *list, void *obj, unsigned int expire_seconds);

// add / update existing item
// NOTE: if item didn't exist, it adds it automatically
// returns pre-existing item if found (in that case, obj will not be used and maybe should be freed
// returns obj if it was used (in that case it should presumably not be freed until it expires
void *expire_list_set(texpire_list *list, void *obj, unsigned int expire_seconds);

// expires all objects that should be expired
// runs optional callback that should free the object's memory
void expire_list_run(texpire_list *list, void (*callback_expire)(void *obj));

// -1 when there are no items, this indicates how long can poll sleep (eg: smallest t, t from first item)
#define expire_list_next_expiracy(l) (l->items ? l->items->t : -1)

// this is used only for test units, no need to make efficient
unsigned int expire_list_count(texpire_list *list);

#ifdef DEBUG_EXPIRE_LIST
void expire_list_print(texpire_list *list, void (*callback_print)(void *obj));
#endif

#endif