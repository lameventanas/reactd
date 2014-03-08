/*
 * $Id$
 */

#include <stdlib.h>
#include "threshold.h"

/*
* records an occurrance in a threshold with this key
* this also updates the triggered status for this key
*/
void threshold_record_occurrance(tthreshold *threshold, char *key) {
	occurrances_rec *occurrances = keylist_get(&(threshold->occurrances), key);
	
	printf("threshold: looking for key %s\n", key);
	if (occurrances == NULL) {
		printf("threshold: doesnt exist, creating\n");
		
		// no occurrance has been recorded yet for this key, create it
		occurrances = malloc(sizeof(occurrances_rec));
		
		occurrances->size = (threshold->config.trigger_count > threshold->config.reset_count ? threshold->config.trigger_count : threshold->config.reset_count);
		
		occurrances->timestamp = malloc(sizeof(time_t) * occurrances->size);
		occurrances->start = 0;
		occurrances->count = 0;
		// associate to this key
		keylist_set(&(threshold->occurrances), key, occurrances);
	}
	
	if (occurrances->count < occurrances->size) {
		// ring not full yet, keep adding to the end
		occurrances->count++;
	} else {
		// will forget the first one
		occurrances->start = (occurrances->start + 1) % occurrances->size;
	}
	
	// record this occurrance in the ring structure
	occurrances->lastupdate = time(NULL);
	occurrances->timestamp[(occurrances->start + occurrances->count - 1) % occurrances->size] = occurrances->lastupdate;
	
	printf("\tstart: %d\n", occurrances->start);
	printf("\tcount: %d\n", occurrances->count);
	printf("\tsize: %d\n", occurrances->size);
	printf("\toccurrances:\n");
	int p;
	for (p = 0; p < occurrances->size; p++) {
		printf("\t\t%d %d\n", p, occurrances->timestamp[(occurrances->start + p) % occurrances->size]);
	}
	printf("\n");
	
	printf("-> Trigger check\n");
	// Check if we should trigger the event for this key
	if (occurrances->triggered == 0 && threshold->config.trigger_period > 0 && threshold->config.trigger_count > 0) {
		printf("Number of occurrances: %d / (should be at least %d to trigger)\n", occurrances->count, threshold->config.trigger_count);
		printf("Time between first and last occurrances: %d (should be at most %d to trigger)\n", (occurrances->lastupdate - occurrances->timestamp[ occurrances->start ]), threshold->config.trigger_period);
		if ((occurrances->count >= threshold->config.trigger_count) &&
		((occurrances->lastupdate - occurrances->timestamp[ occurrances->start ]) <= threshold->config.trigger_period)) {
			occurrances->triggered = 1;
			printf(" *** TRIGGERED!\n");
		}
	} else {
		printf("Threshold trigger not configured or already triggered\n");
	}
}

/* update the triggered status for all keys (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 */
void threshold_update_status(tthreshold *threshold) {
	
	keylist *key;
	occurrances_rec *occurrances;
	time_t t = time(NULL);
	
	for (key = threshold->occurrances; key != NULL; key = key->next) {
		printf("reset: checking occurrances for key %s\n", key->key);
		occurrances = key->value;
		// Check if we should reset the trigger event for this key
		if (occurrances->triggered == 1 && threshold->config.reset_period > 0 && threshold->config.reset_count > 0) {
			printf("reset: event triggered and reset configured, checking for reset\n");
			if (threshold->config.reset_count <= occurrances->count) {
				// find position of the first occurrance to consider to reset the event
				// ie: if reset_count = 3, find 3rd from last occurrance
				int p;
				p = (occurrances->start + occurrances->count - 1) % occurrances->size; // now p = last
				printf("reset: last position: %d\n", p);
				p = p - threshold->config.reset_count + 1;
				printf("reset: last position minus reset_count: %d\n", p);
				if (p < 0) { // count from last
					printf("reset: fixing negative last\n");
					p = occurrances->size + p + 1;
				}
				printf("reset: first position to consider for reset: %d\n", p);
				printf("reset: first timestamp to consider for reset: %d\n", occurrances->timestamp[p]);
				printf("reset: current timestamp: %d\n", t);
				printf("reset: elapsed time: %d\n", t - occurrances->timestamp[p]);
				
				if (t - occurrances->timestamp[p] >= threshold->config.reset_period) {
					printf(" *** RESET!\n");
					occurrances->triggered = 0;
				}
			}
		}
	}
}
