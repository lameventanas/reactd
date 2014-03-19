/*
 * $Id$
 */
#ifndef THRESHOLD_H
#define THRESHOLD_H

#include <time.h>
#include "keylist.h"
#include "pcre_subst.h"

/*
 * threshold library: Keeps the configuration for an event threshold, and tracks occurrances and triggered status
 */


/*
 * threshold structure: keeps configuration and tracks occurrances and triggered status
 */
typedef struct {
	struct {
		char *key;
		pcre_subst_data *re_subst_key; // constructed from key
		int trigger_count;
		int trigger_period;
		int reset_count;
		int reset_period;
		char *reset_cmd;
	} config;
	keylist *occurrances; // hash of key => occurrances_rec
} tthreshold;


/*
 * occurrance record: keeps track of the last <trigger count> occurrances (or the last <reset count> occurrances, if higher)
 * If the trigger threshold is reached, the "triggered" status is updated
 * Thereafeter, if the reset threshold is reached, the "triggered" status is reset
 */
typedef struct {
	int lastupdate; // records when was the last update (to prevent unnecessary updates to triggered status UNUSED?)
	int start; // pointer to first timestamp occurrance in the ring
	int count; // number of elements stored
	int size; // size of the ring, this is max(threshold_count, reset_count)
	time_t *timestamp; // holds last X timestamps of occurrances in a circular buffer
	int triggered; // 0 = normal, 1 = triggered
} occurrances_rec;

/*
 * records an occurrance in a threshold with this key
 * this also updates the triggered status for this key
 */
void threshold_record_occurrance(tthreshold *threshold, char *key);

/* update the triggered status for all keys (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 */
void threshold_update_status(tthreshold *threshold);

#endif
