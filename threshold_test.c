#include <stdio.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include "threshold.h"


#define NUMKEYS

pthread_mutex_t mutex;

void *update(void *arg) {
    time_t t;
    keylist *thkey;

    while (1) {
        usleep(1000000); // 1 second
        pthread_mutex_lock(&mutex);
        t = time(NULL);
        printf("update: Updating threshold t=%d\n", t);
        for (thkey = ((tthreshold *)arg)->occurrances; thkey; thkey = thkey->next)
            threshold_update_status((tthreshold *)arg, thkey->key);
        pthread_mutex_unlock(&mutex);
    }
}

int main(int argc, char *argv[]) {
    char key[50];
    tthreshold threshold;
    pthread_t tid;

    keylist *thkey;

    memset(&threshold, 0, sizeof(threshold));
    threshold.config.trigger_count = 5;
    threshold.config.trigger_period = 10;
    threshold.config.reset_count = 3;
    threshold.config.reset_period = 5;

    pthread_mutex_init(&mutex, NULL);

    pthread_create(&tid, NULL, update, &threshold);

    printf("Type in a key to record an occurance\n");
    while (1) {
        scanf("%s", key);
        if (!strcmp(key, "quit"))
            break;

        pthread_mutex_lock(&mutex);
        printf("Will register this time: %d\n", time(NULL));

        threshold_record_occurrance(&threshold, key);

        for (thkey = threshold.occurrances; thkey != NULL; thkey = thkey->next) {
            occurrances_rec *occurrances;
            int p;

            printf("key: %s\n", thkey->key);
            occurrances = thkey->value;
            // printf("\tlastupdate: %d\n", occurrances->lastupdate);
            printf("\tstart: %d\n", occurrances->start);
            printf("\tcount: %d\n", occurrances->count);
            printf("\ttriggered: %d\n", occurrances->triggered);
            printf("\toccurrances:\n");
            for (p = 0; p < occurrances->count; p++) {
                printf("\t%d %d\n", p, occurrances->timestamp[p]);
            }
            printf("\n");
        }

        pthread_mutex_unlock(&mutex);
    }
}

