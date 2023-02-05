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

// resizes ring
// returns true if successful (can't resize below current item count)
int ring_resize(ring *ring, unsigned int size);

// returns number of nodes in ring
#define ring_count(r) r->count

// get ring size
#define ring_size(r) r->size

// get first value
#define ring_oldest(r) (r->count > 0 ? r->values[r->first] : NULL)

// get last value
#define ring_newest(r) (r->count > 0 ? r->values[(r->first + r->count - 1) % r->size ] : NULL )


#ifdef DEBUG_RING
// sets default callback function used to print values, for debugging
void ring_set_debug_callback(ring *ring, void (*callback)(void *));

// print debug information, callback is optional
void ring_print(ring *ring, void (*callback)(void *));
#endif

#endif
