/*
 * $Id$
 */

#include <stdlib.h>
#include "threshold.h"
#include "debug.h"

/*
* records an occurrance in a threshold with this key
* this also updates the triggered status for this key
* returns true if this occurrance triggers the event
*/
int threshold_record_occurrance(tthreshold *threshold, char *key) {
	time_t t = time(NULL);
	
	occurrances_rec *occurrances = keylist_get(&(threshold->occurrances), key);
	
	if (occurrances == NULL) {
		dprint("First occurrance for key %s", key);
		// first occurrance for this key, allocate and initialize occurrance ring
		occurrances = malloc(sizeof(occurrances_rec));
		occurrances->size = (threshold->config.trigger_count > threshold->config.reset_count ? threshold->config.trigger_count : threshold->config.reset_count);
		occurrances->timestamp = malloc(sizeof(time_t) * occurrances->size);
		occurrances->start = 0;
		occurrances->count = 0;
		occurrances->triggered = 0;
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
	
	// now check if we should trigger the action for this key
	
	// not triggered yet, and a triggered is configured for this threshold
	if (occurrances->triggered == 0 && threshold->config.trigger_count > 0 && threshold->config.trigger_period > 0) {
		
		// find position of the first occurrance to consider to trigger the event
		// ie: if trigger_count = 3, find 3rd from last occurrance
		int p;
		p = (occurrances->start + occurrances->count - 1) % occurrances->size; // now p = last
		p = p - threshold->config.trigger_count + 1;
		if (p < 0) { // count from last
			p = occurrances->size + p + 1;
		}
		// now p points to the first occurrance to consider for a trigger
		/*
		if (t - occurrances->timestamp[p] >= threshold->config.reset_period) {
			// the time elapsed between p and now is greater or equal than the reset period
			// so we have to reset
			occurrances->triggered = 0;
		}
		*/
		
		dprint("threshold_record_occurrance: %d sec has passed since last %d occurrances for %s", t - occurrances->timestamp[p], (occurrances->count) < (threshold->config.trigger_count) ? (occurrances->count) : (threshold->config.trigger_count), key);
		
		// number of occurrances is at least equal to threshold's trigger count and the time elapsed between the first one to consider and the last one is less than the threshold's trigger period
		if ((occurrances->count >= threshold->config.trigger_count) &&
		((t - occurrances->timestamp[p]) <= threshold->config.trigger_period)) {
			occurrances->triggered = 1;
			return 1; // just triggered now
		}
	}
	return 0;
}

/* update the triggered status for a key (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 * returns true if trigger is reset
 */
int threshold_update_status(tthreshold *threshold, char *key) {
	time_t t = time(NULL);
	occurrances_rec *occurrances = keylist_get(&(threshold->occurrances), key);
	
	// check if the action has been triggered
	dprint("threshold_update_status: %s triggered: %d", key, occurrances->triggered);
	if (occurrances->triggered == 1) {
		// find position of the first occurrance to consider to reset the event
		// ie: if reset_count = 3, find 3rd from last occurrance
		int p;
		p = (occurrances->start + occurrances->count - 1) % occurrances->size; // now p = last
		p = p - threshold->config.reset_count + 1;
		if (threshold->config.reset_count == 0)
			p--;
		if (p < 0) { // count from last
			p = occurrances->size + p + 1;
		}
		// now p points to the first occurrance to consider for a reset
		dprint("threshold_update_status: %d have passed since last %d occurrances for %s", t - occurrances->timestamp[p], threshold->config.reset_count, key);
		if (t - occurrances->timestamp[p] >= threshold->config.reset_period) {
			// the time elapsed between p and now is greater or equal than the reset period
			// so we have to reset
			occurrances->triggered = 0;
			return 1; // triggered status reset right now
		}
	}
	return 0;
}
