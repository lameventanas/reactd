#ifndef RING_H
#define RING_H

// Implements ring with fixed-size array
// More memory-efficient than using lists, but less intuitive

typedef struct {
    void **values;
    unsigned int first;
    unsigned int count;
    unsigned int size;
#ifdef DEBUG_RING
    void (*debug_callback)(void *);
#endif
} ring;

ring *ring_init(unsigned int size);

// frees ring
// calls callback to free ring values
void ring_free(ring *ring, void (*callback)(void *));

// adds new value
// removes and returns oldest value if ring is full
void *ring_put(ring *ring, void *value);

// returns newest value
void *ring_get_newest(ring *ring, int remove);

// returns oldest value
void *ring_get_oldest(ring *ring, int remove);

// returns number of nodes in ring
unsigned int ring_count(ring *ring);

// resizes ring
// returns true if successful (can't resize below current item count)
int ring_resize(ring *ring, unsigned int size);

// get ring size
#define ring_get_size(r) r->size

#ifdef DEBUG_RING
// sets default callback function used to print values, for debugging
void ring_set_debug_callback(ring *ring, void (*callback)(void *));

// print debug information, callback is optional
void ring_print(ring *ring, void (*callback)(void *));
#endif

#endif
