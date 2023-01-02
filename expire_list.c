#include <stdlib.h>
#include <assert.h>
#include "expire_list.h"

#ifdef DEBUG_EXPIRE_LIST
#include <stdio.h>
#define dprintf printf
#else
#define dprintf
#endif

texpire_list *expire_list_init(int (*cmp)(const void *v1, const void *v2)) {
    texpire_list *list = malloc(sizeof(texpire_list));
    assert(list != NULL);
    list->items = NULL;
    list->cmp = cmp;
    return list;
}

void expire_list_free(texpire_list *list,  void (*callback_free)(void *obj)) {
    assert(list != NULL);
    texpire_item *item = list->items;
    while (item) {
        if (callback_free)
            (callback_free)(item->obj);
        texpire_item *tmp = item;
        item = item->next;
        free(tmp);
    }
    free(list);
}

// inserts new object at appropriate position
void expire_list_add(texpire_list *list, void *obj, unsigned int expire_seconds) {
    assert(list != NULL);
    time_t t = time(NULL) + expire_seconds;

    // find position of previous item ascending by t
    texpire_item *prev = NULL;
    for (texpire_item *item = list->items; item != NULL; item = item->next) {
        if (item->t >= t)
            break;
        prev = item;
    }
    texpire_item *item = malloc(sizeof(texpire_item));
    assert(item != NULL);
    item->obj = obj;
    item->t = t;
    if (prev) {
        item->next = prev->next;
        prev->next = item;
    } else {
        // insert at beginning
        item->next = list->items;
        list->items = item;
    }
}

// this is basically a delete followed by an add
// uses cmp function to find item if set (if cmp is unset, compares pointers directly)
void *expire_list_set(texpire_list *list, void *obj, unsigned int expire_seconds) {
    time_t t = time(NULL) + expire_seconds;

    // find item
    texpire_item *prev = NULL;
    texpire_item *item = list->items;

    if (list->cmp == NULL) {
        for (; item != NULL; item = item->next) {
            if (item->obj == obj)
                break;
            prev = item;
        }
    } else {
        for (; item != NULL; item = item->next) {
            if (list->cmp(item->obj, obj) == 0)
                break;
            prev = item;
        }
    }
    if (item) {
        // something found, update timestamp
        item->t = t;
        // if it wasn't the first, we need to reposition it to the beginning
        if (prev) {
            // skip this item in list
            prev->next = item->next;
            // make this item the first
            item->next = list->items;
            list->items = item;
        }
    } else {
        // nothing found, add it at the beginning
        texpire_item *item = malloc(sizeof(texpire_item));
        assert(item != NULL);
        item->t = t;
        item->obj = obj;
        item->next = list->items;
        list->items = item;
    }

    return item->obj;
}

void expire_list_run(texpire_list *list, void (*callback_expire)(void *obj)) {
    time_t t = time(NULL);
    dprintf("expiring objects with t <= %d\n", t);
    while (list->items != NULL && t >= list->items->t) {
        dprintf("    expiring object t=%d 0x%X\n", list->items->t, list->items->obj);
        if (callback_expire)
            callback_expire(list->items->obj);
        texpire_item *tmp = list->items;
        list->items = list->items->next;
        free(tmp);
    }
}

unsigned int expire_list_count(texpire_list *list) {
    assert(list != NULL);
    unsigned int ret = 0;
    for (texpire_item *item = list->items; item != NULL; item = item->next)
        ret ++;
    return ret;
}

#ifdef DEBUG_EXPIRE_LIST
void expire_list_print(texpire_list *list, void (*callback_print)(void *obj)) {
    if (list == NULL) {
        dprintf("expire_list is null\n");
        return;
    }
    dprintf("printing expire_list with %u items: -->\n", expire_list_count(list));
    unsigned int i = 0;
    for (texpire_item *item = list->items; item; item = item->next)
        if (callback_print) {
            dprintf("%u: %p t=%d next=%p obj=", i++, item, item->t, item->obj, item->next);
            (callback_print)(item->obj);
        } else {
            dprintf("%u: %p t=%d next=%p obj=0x%X\n", i++, item, item->t, item->obj, item->next);
        }
    dprintf("<--\n");
}
#endif