/*
 * $Id$
 */
#ifndef THRESHOLD_H
#define THRESHOLD_H

/*
 * threshold library: Keeps the configuration for an event threshold, and tracks occurrances and triggered status
 */


/*
 * threshold structure: keeps configuration and tracks occurrances and triggered status
 */
typedef struct {
	struct {
		char *key;
		int trigger_count;
		int trigger_period;
		int reset_count;
		int reset_period;
		char *reset_cmd;
	} config;
	keylist *occurrances; // hash of key => occurrance_rec
} tthreshold;


/*
 * occurrance record: keeps track of the last <trigger count> occurrances (or the last <reset count> occurrances, if higher)
 * If the trigger threshold is reached, the "triggered" status is updated
 * Thereafeter, if the reset threshold is reached, the "triggered" status is reset
 */
typedef struct {
	int lastupdate; // records when was the last update (to prevent unnecessary updates)
	int first; // first timestamp occurrance (index to first in array)
	int last; // last timestamp occurrance (index to last in array)
	time_t *timestamp; // this array holds the last X timestamps of occurrances in a ring structure (x = max(threshold_count, reset_count)
	int triggered; // 0 = normal, 1 = triggered
} occurrance_rec;

/*
 * records an occurrance in a threshold with this key
 * this also updates the triggered status for this key
 */
void threshold_record_occurrance(tthreshold *threshold, char *key);

/* update the triggered status for all keys (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 */
void threshold_update_status(tthreshold *threshold);

#endif
