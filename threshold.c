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
	time_t t = time(NULL);
	
	occurrances_rec *occurrances = keylist_get(&(threshold->occurrances), key);
	
	if (occurrances == NULL) {
		// first occurrance for this key, allocate and initialize occurrance ring
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
		// ring full: forget the first one
		occurrances->start = (occurrances->start + 1) % occurrances->size;
	}
	
	// record this occurrance in the ring
	occurrances->timestamp[(occurrances->start + occurrances->count - 1) % occurrances->size] = t;
	
	// nowcheck if we should trigger the action for this key
	
	// not triggered yet, and a triggered is configured for this threshold
	if (occurrances->triggered == 0 && threshold->config.trigger_period > 0 && threshold->config.trigger_count > 0) {
		
		// find position of the first occurrance to consider to trigger the event
		// ie: if trigger_count = 3, find 3rd from last occurrance
		int p;
		p = (occurrances->start + occurrances->count - 1) % occurrances->size; // now p = last
		p = p - threshold->config.trigger_count + 1;
		if (p < 0) { // count from last
			p = occurrances->size + p + 1;
		}
		// now p points to the first occurrance to consider for a trigger
		if (t - occurrances->timestamp[p] >= threshold->config.reset_period) {
			// the time elapsed between p and now is greater or equal than the reset period
			// so we have to reset
			occurrances->triggered = 0;
		}
		
		// number of occurrances is at least equal to threshold's trigger count and the time elapsed between the first one to consider and the last one is less than the threshold's trigger period
		if ((occurrances->count >= threshold->config.trigger_count) &&
		((t - occurrances->timestamp[p]) <= threshold->config.trigger_period)) {
			occurrances->triggered = 1;
		}
	}
}

/* update the triggered status for all keys (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 */
void threshold_update_status(tthreshold *threshold) {
	keylist *key;
	occurrances_rec *occurrances;
	time_t t = time(NULL);
	
	// if no reset threshold is configured, there is nothing to reset
	if (!(threshold->config.reset_period > 0 && threshold->config.reset_count > 0))
		return;
	
	// check all keys for this threshold
	for (key = threshold->occurrances; key != NULL; key = key->next) {
		occurrances = key->value;
		
		// check if the action has been triggered
		if (occurrances->triggered == 1) {
			// if enough occurrances have been recorded to consider a reset
			if (threshold->config.reset_count <= occurrances->count) {
				// find position of the first occurrance to consider to reset the event
				// ie: if reset_count = 3, find 3rd from last occurrance
				int p;
				p = (occurrances->start + occurrances->count - 1) % occurrances->size; // now p = last
				p = p - threshold->config.reset_count + 1;
				if (p < 0) { // count from last
					p = occurrances->size + p + 1;
				}
				// now p points to the first occurrance to consider for a reset
				if (t - occurrances->timestamp[p] >= threshold->config.reset_period) {
					// the time elapsed between p and now is greater or equal than the reset period
					// so we have to reset
					occurrances->triggered = 0;
				}
			}
		}
	}
}
