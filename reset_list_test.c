#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "CuTest.h"
#include "reset_list.h"

void list_callback(char *key, char *cmd) {
    printf(" * callback received item with key=%s\n", key);
}

void TestOne(CuTest *tc) {
    printf("\nTestOne starting\n");
    treset_list *list = reset_list_init();

    puts("before start");
    reset_list_print(list);
    CuAssertIntEquals(tc, 0, reset_list_count(list));
    CuAssertIntEquals(tc, -1, reset_list_next_reset(list));

    puts("adding t=10");
    reset_list_add(list, 10, "t10", NULL);
    reset_list_print(list);
    CuAssertIntEquals(tc, 1, reset_list_count(list));
    CuAssertIntEquals(tc, 10, reset_list_next_reset(list));

    puts("run t=5");
    reset_list_run(list, 5, list_callback);
    reset_list_print(list);
    CuAssertIntEquals(tc, 1, reset_list_count(list));
    CuAssertIntEquals(tc, 10, reset_list_next_reset(list));

    puts("run t=15");
    reset_list_run(list, 15, list_callback);
    reset_list_print(list);
    CuAssertIntEquals(tc, 0, reset_list_count(list));
    CuAssertIntEquals(tc, -1, reset_list_next_reset(list));

}

void TestTwo(CuTest *tc) {
    printf("\nTestTwo starting\n");
    treset_list *list = reset_list_init();

    puts("before start");
    reset_list_print(list);
    CuAssertIntEquals(tc, 0, reset_list_count(list));
    CuAssertIntEquals(tc, -1, reset_list_next_reset(list));

    puts("adding t=10");
    reset_list_add(list, 10, "t10", NULL);
    puts("adding t=20");
    reset_list_add(list, 20, "t20", NULL);
    reset_list_print(list);
    CuAssertIntEquals(tc, 2, reset_list_count(list));
    CuAssertIntEquals(tc, 10, reset_list_next_reset(list));

    puts("run t=5");
    reset_list_run(list, 5, list_callback);
    reset_list_print(list);
    CuAssertIntEquals(tc, 2, reset_list_count(list));
    CuAssertIntEquals(tc, 10, reset_list_next_reset(list));

    puts("run t=15");
    reset_list_run(list, 15, list_callback);
    reset_list_print(list);
    CuAssertIntEquals(tc, 1, reset_list_count(list));
    CuAssertIntEquals(tc, 20, reset_list_next_reset(list));

    puts("run t=30");
    reset_list_run(list, 30, list_callback);
    reset_list_print(list);
    CuAssertIntEquals(tc, 0, reset_list_count(list));
    CuAssertIntEquals(tc, -1, reset_list_next_reset(list));
}


CuSuite *GetSuite() {
    CuSuite* suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, TestOne);
    SUITE_ADD_TEST(suite, TestTwo);
    return suite;
}
