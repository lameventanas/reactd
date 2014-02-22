/*
 * $Id$
 */
#include event_threshold.h

/*
* records an occurrance in a threshold with this key
* this also updates the triggered status for this key
*/
void threshold_record_occurrance(tthreshold *threshold, char *key) {
}

/* update the triggered status for all keys (should be called periodically to reset the actions, every 1 minute because that is the granularity for trigger periods)
 */
void threshold_update_status(tthreshold *threshold) {
}
