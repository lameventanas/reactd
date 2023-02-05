#include "ring.h"
#include <stdlib.h>
#include <assert.h>

#ifdef DEBUG_RING
#include <stdio.h>
#endif

ring *ring_init(unsigned int size) {
    assert(size > 0);
    ring *ret  = calloc(1, sizeof(ring));
    ret->values = calloc(size, sizeof(void *));
    ret->size  = size;
    return ret;
}

void ring_free(ring *ring, void (*callback)(void *)) {
#ifdef DEBUG_RING
    printf("\nring_free (before):\n");
    ring_print(ring, NULL);
#endif
    if (callback) {
        unsigned int i = 0;
        for (unsigned int i = 0; i < ring->count; i++) {
            void *value = ring->values[(ring->first + i) % ring->size];
#ifdef DEBUG_RING
            printf("ring_free will free position %u: %p\n", i, value);
#endif
            callback(value);
        }
    }
    free(ring->values);
    free(ring);
}

// adds new value
// removes and returns oldest value if ring is full
void *ring_put(ring *ring, void *value) {
#ifdef DEBUG_RING
    printf("\nring_put (before):\n");
    ring_print(ring, NULL);
#endif
    void *ret = NULL;

    if (ring->count == 0) {
        ring->values[0] = value;
    } else {
        unsigned int pos = (ring->first + ring->count) % ring->size;
        if (pos == ring->first) {
            ret = ring->values[pos];
            ring->first = (ring->first + 1) % ring->size;
        }
        ring->values[pos] = value;
    }
    if (ring->count < ring->size)
        ring->count ++;

#ifdef DEBUG_RING
    printf("ring_put (after):\n");
    ring_print(ring, NULL);
    printf("ring_put: returning %p\n\n", ret);
#endif
    return ret;
}

// returns newest value
void *ring_get_newest(ring *ring, int remove) {
#ifdef DEBUG_RING
    printf("\nring_get_newest remove=%d (before):\n",remove);
    ring_print(ring, NULL);
#endif
    void *ret = NULL;

    if (ring->count > 0) {
        ret = ring->values[(ring->first + ring->count - 1) % ring->size];
        if (remove)
            ring->count = (ring->count - 1) % (ring->size + 1);
    }

#ifdef DEBUG_RING
    printf("ring_get_newest remove=%d (after):\n", remove);
    ring_print(ring, NULL);
    printf("ring_get_newest: returning %p\n\n", ret);
#endif
    return ret;
}

// returns oldest value
void *ring_get_oldest(ring *ring, int remove) {
#ifdef DEBUG_RING
    printf("ring_get_oldest remove=%d (before):\n", remove);
    ring_print(ring, NULL);
#endif
    void *ret = NULL;

    if (ring->count > 0) {
        ret = ring->values[ring->first];
        if (remove) {
            ring->first = (ring->first + 1) % ring->size;
            ring->count --;
        }
    }

#ifdef DEBUG_RING
    printf("ring_get_oldest remove=%d (after):\n", remove);
    ring_print(ring, NULL);
    printf("ring_get_oldest: returning %p\n\n", ret);
#endif
    return ret;
}

int ring_resize(ring *ring, unsigned int size) {
#ifdef DEBUG_RING
    printf("ring_resize %u -> %u (before):\n", ring->size, size);
    ring_print(ring, NULL);
#endif
    assert(size > 0);
    if (ring->count > size) {
#ifdef DEBUG_RING
        printf("count > size: %u > %u\n", ring->count, size);
#endif
        return 0;
    }
    ring->values = realloc(ring->values, size * sizeof(void *));

    // unroll values and copy to newly allocated memory
    if (ring->first + ring->count > ring->size) {
        // printf("resize: copying values\n");
        for (unsigned int i = 0; i < (ring->first + ring->count) % ring->size; i++) {
            // printf("i=%u copying value from %u to %u\n", i, i, ring->size + i);
            ring->values[ring->size + i] = ring->values[i];
        }
    }

#ifdef DEBUG_RING
    printf("ring_resize %u -> %u (after):\n", ring->size, size);
#endif
    ring->size = size;
#ifdef DEBUG_RING
    ring_print(ring, NULL);
#endif
    return 1;
}

#ifdef DEBUG_RING
// sets default callback function used to print values, for debugging
void ring_set_debug_callback(ring *ring, void (*callback)(void *)) {
    ring->debug_callback = callback;
}

// print debug information, callback is optional
void ring_print(ring *ring, void (*callback)(void *)) {
    printf("\tRING: count:%u size:%u first:%u last:%u\n", ring->count, ring->size, ring->first, ring->count ? (ring->first + ring->count - 1) % ring->size : 0);

    for (unsigned int i = 0; i < ring->count; i++) {
        printf("\t[%u] %-5s value:%p: ", i, ( i == ring->first ? "first" : (i == (ring->first + ring->count - 1) % ring->size ? "last" : "mid")), ring->values[i]);
        if (callback)
            callback(ring->values[i]);
        else
            if (ring->debug_callback)
                ring->debug_callback(ring->values[i]);
            else
                puts("");
    }
}
#endif
