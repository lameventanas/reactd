#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "CuTest.h"
#include "expire_list.h"

#ifndef DEBUG_EXPIRE_LIST
#define expire_list_print(x)
#endif

static char *o1 = "uno";
static char *o2 = "dos";
static char *o3 = "tres";
static char *o4 = "cuatro";
static char *o5 = "cinco";
static char *o6 = "seis";
static char *o7 = "siete";
static char *o8 = "ocho";
static char *o9 = "nueve";

char *most_recently_expired = NULL;

time_t t = 0;

void callback_expire(void *obj) {
    printf(" * expired object: %s\n", obj);
    most_recently_expired = obj;
}

void callback_print(void *obj) {
    puts((char *)obj);
}

void TestOne(CuTest *tc) {
    printf("\nTestOne starting\n");
    texpire_list *list = expire_list_init(strcmp);

    printf("t=%d before start\n", time(NULL));
    expire_list_print(list, callback_print);
    CuAssertIntEquals(tc, 0, expire_list_count(list));
    CuAssertIntEquals(tc, -1, expire_list_next_expiracy(list));

    printf("t=%d adding o1 expire_seconds=2\n", time(NULL));
    expire_list_add(list, o1, 2);
    expire_list_print(list, callback_print);
    CuAssertIntEquals(tc, 1, expire_list_count(list));

    t = time(NULL);
    printf("t=%d checking next expiracy\n", t);
    CuAssertTrue(tc, expire_list_next_expiracy(list) == t+1 || expire_list_next_expiracy(list) == t+2);

    printf("t=%d sleeping 1 second\n", time(NULL));
    sleep(1);
    printf("t=%d adding o2 expire_seconds=2\n", time(NULL));
    expire_list_add(list, o2, 2);
    expire_list_print(list, callback_print);
    CuAssertIntEquals(tc, 2, expire_list_count(list));

    printf("t=%d sleeping 1 seconds (o1 expires after this)\n", time(NULL));
    sleep(1);
    printf("t=%d adding o3 expire_seconds=2\n", time(NULL));
    expire_list_add(list, o3, 2);
    expire_list_print(list, callback_print);
    CuAssertIntEquals(tc, 3, expire_list_count(list));

    expire_list_run(list, callback_expire);
    expire_list_print(list, callback_print);
    CuAssertStrEquals(tc, o1, most_recently_expired);
    CuAssertIntEquals(tc, 2, expire_list_count(list));

    printf("t=%d sleeping 1 second (o2 expires after this)\n", time(NULL));
    sleep(1);
    printf("t=%d expring items\n", time(NULL));
    expire_list_run(list, callback_expire);
    expire_list_print(list, callback_print);
    CuAssertStrEquals(tc, o2, most_recently_expired);
    CuAssertIntEquals(tc, 1, expire_list_count(list));

    printf("t=%d sleeping 2 seconds (o3 expires after this)\n", time(NULL));
    sleep(2);
    printf("t=%d expring items\n", time(NULL));
    expire_list_run(list, callback_expire);
    expire_list_print(list, callback_print);
    CuAssertStrEquals(tc, o3, most_recently_expired);
    CuAssertIntEquals(tc, 0, expire_list_count(list));

    CuAssertIntEquals(tc, -1, expire_list_next_expiracy(list));
}


CuSuite *GetSuite() {
    CuSuite* suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, TestOne);
    return suite;
}
