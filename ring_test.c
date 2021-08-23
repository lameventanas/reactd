#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CuTest.h"

#include "ring.h"

#ifndef DEBUG_RING
#define ring_set_debug_callback(a, b)
#define ring_print(a, b)
#endif

void print_str(void *s) {
    puts(s);
}

void TestSize1(CuTest *tc) {
    char *value = NULL;
    puts("TestSize1");

    puts("init");
    ring *r = ring_init(1);
    ring_set_debug_callback(r, print_str);
    CuAssertIntEquals(tc, 0, ring_count(r));

    puts("put uno");
    value = strdup("uno");
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);
    CuAssertIntEquals(tc, 1, ring_count(r));

    puts("put dos");
    value = strdup("dos");
    value = ring_put(r, value);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "uno", value);
    free(value);
    CuAssertIntEquals(tc, 1, ring_count(r));

    puts("get_newest, remove=1 dos");
    value = ring_get_newest(r, 1);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "dos", value);
    free(value);

    CuAssertIntEquals(tc, 0, ring_count(r));

    puts("free");
    ring_free(r, free);

    puts("--------------------");
}

void TestSize3(CuTest *tc) {
    char *value = NULL;
    puts("TestSize3");

    puts("init");
    ring *r = ring_init(3);
    ring_set_debug_callback(r, print_str);
    CuAssertIntEquals(tc, 0, ring_count(r));

    value = strdup("uno");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);
    CuAssertIntEquals(tc, 1, ring_count(r));

    value = strdup("dos");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);
    CuAssertIntEquals(tc, 2, ring_count(r));

    value = strdup("tres");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);
    CuAssertIntEquals(tc, 3, ring_count(r));

    ring_print(r, NULL);

    puts("get_oldest uno");
    value = ring_get_oldest(r, 0);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "uno", value);
    CuAssertIntEquals(tc, 3, ring_count(r));

    puts("get_newest tres");
    value = ring_get_newest(r, 0);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "tres", value);
    CuAssertIntEquals(tc, 3, ring_count(r));

    puts("get_oldest uno, remove");
    value = ring_get_oldest(r, 1);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "uno", value);
    free(value);
    CuAssertIntEquals(tc, 2, ring_count(r));

    puts("get_newest tres, remove");
    value = ring_get_newest(r, 1);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "tres", value);
    free(value);
    CuAssertIntEquals(tc, 1, ring_count(r));

    puts("get_newest dos, remove");
    value = ring_get_newest(r, 1);
    CuAssertPtrNotNull(tc, value);
    CuAssertStrEquals(tc, "dos", value);
    free(value);
    CuAssertIntEquals(tc, 0, ring_count(r));

    value = strdup("cuatro");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);
    CuAssertIntEquals(tc, 1, ring_count(r));

    puts("free");
    ring_free(r, free);

    puts("--------------------");
}

void TestResize(CuTest *tc) {
    char *value = NULL;
    puts("TestResize");

    puts("init");
    ring *r = ring_init(2);
    ring_set_debug_callback(r, print_str);
    CuAssertIntEquals(tc, 0, ring_count(r));

    value = strdup("uno");
    printf("put %s\n", value);
    ring_put(r, value);
    value = ring_get_newest(r, 0);
    CuAssertStrEquals(tc, "uno", value);

    value = strdup("dos");
    printf("put %s\n", value);
    ring_put(r, value);
    value = ring_get_newest(r, 0);
    CuAssertStrEquals(tc, "dos", value);

    // ring is size 2, so a third push should return "uno"
    value = strdup("tres");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertStrEquals(tc, "uno", value);
    free(value);

    value = ring_get_newest(r, 0);
    CuAssertStrEquals(tc, "tres", value);
    value = ring_get_oldest(r, 0);
    CuAssertStrEquals(tc, "dos", value);

    puts("resizing");
    ring_resize(r, 3);

    // ring was resized to 3, so a third push should fit and it should return NULL
    value = strdup("cuatro");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertPtrEquals(tc, NULL, value);

    value = ring_get_oldest(r, 0);
    CuAssertStrEquals(tc, "dos", value);

    // ring has 3 items, so this push should return dos
    value = strdup("cinco");
    printf("put %s\n", value);
    value = ring_put(r, value);
    CuAssertStrEquals(tc, "dos", value);

    value = ring_get_oldest(r, 0);
    CuAssertStrEquals(tc, "tres", value);

    puts("free");
    ring_free(r, free);

    puts("--------------------");
}

CuSuite* RingGetSuite() {
    CuSuite* suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, TestSize1);
    SUITE_ADD_TEST(suite, TestSize3);
    SUITE_ADD_TEST(suite, TestResize);
    return suite;
}
