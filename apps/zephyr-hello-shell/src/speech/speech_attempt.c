/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_service.h"
#include <string.h>

_Static_assert(SPEECH_LABEL_COUNT - SPEECH_YES == SPEECH_KEYWORD_COUNT,
	       "attempt keyword/label mismatch");

void speech_attempt_reset(struct speech_attempt_tracker *tracker)
{
	memset(tracker, 0, sizeof(*tracker));
	tracker->quiet_since = -1;
}

static void finish(struct speech_attempt_tracker *tracker, int64_t now, bool interrupted)
{
	tracker->live.end_ms = now;
	tracker->live.interrupted = interrupted;
	tracker->last = tracker->live;
	memset(&tracker->live, 0, sizeof(tracker->live));
	tracker->quiet_since = -1;
}

void speech_attempt_interrupt(struct speech_attempt_tracker *tracker, int64_t now)
{
	if (tracker->live.valid) { finish(tracker, now, true); }
}

void speech_attempt_update(struct speech_attempt_tracker *tracker,
			   const uint16_t scores[], unsigned int winner,
			   int64_t now, bool accepted, unsigned int event)
{
	bool keyword = winner >= SPEECH_YES && winner < SPEECH_LABEL_COUNT;
	if (!tracker->live.valid) {
		if (!keyword) { return; }
		tracker->live.valid = true;
		if (++tracker->sequence == 0) { ++tracker->sequence; }
		tracker->live.sequence = tracker->sequence;
		tracker->live.start_ms = now;
		tracker->quiet_since = -1;
	}
	for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
		if (scores[SPEECH_YES + i] > tracker->live.peaks[i]) {
			tracker->live.peaks[i] = scores[SPEECH_YES + i];
		}
	}
	if (accepted && event >= SPEECH_YES && event < SPEECH_LABEL_COUNT) {
		tracker->live.accepted[event - SPEECH_YES] = true;
	}
	if (keyword) {
		tracker->quiet_since = -1;
	} else {
		if (tracker->quiet_since < 0) { tracker->quiet_since = now; }
		if (now - tracker->quiet_since >= 300) { finish(tracker, now, false); }
	}
}
