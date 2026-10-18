/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_decision.h"
#include <string.h>

void speech_decision_reset(struct speech_decision *decision)
{
	memset(decision, 0, sizeof(*decision));
	decision->threshold_permille = SPEECH_THRESHOLD_DEFAULT_PERCENT * 10U;
	decision->timing = (struct speech_timing)SPEECH_TIMING_DEFAULT;
	decision->armed = true;
	decision->quiet_since = -1;
	decision->last_event = INT64_MIN / 2;
	decision->last_update = -100;
}
void speech_decision_set_threshold(struct speech_decision *decision, uint16_t threshold_permille)
{
	if (decision->threshold_permille == threshold_permille) { return; }
	decision->threshold_permille = threshold_permille;
	decision->count = 0;
}
void speech_decision_set_timing(struct speech_decision *decision, const struct speech_timing *timing)
{
	decision->timing = *timing;
	decision->count = 0;
}
bool speech_decision_update(struct speech_decision *decision, const uint16_t scores[SPEECH_LABEL_COUNT],
			    int64_t uptime_ms, enum speech_label *event)
{
	if (uptime_ms - decision->last_update < 100) { return false; }
	decision->last_update = uptime_ms;
	unsigned int winner = 0;
	bool tie = false;
	for (unsigned int i = 1; i < SPEECH_LABEL_COUNT; ++i) {
		if (scores[i] > scores[winner]) { winner = i; tie = false; }
		else if (scores[i] == scores[winner]) { tie = true; }
	}
	bool keyword = !tie && winner >= SPEECH_YES &&
		       scores[winner] >= decision->threshold_permille;
	if (!keyword) {
		decision->count = 0;
		if (decision->quiet_since < 0) { decision->quiet_since = uptime_ms; }
		if (uptime_ms - decision->quiet_since >= decision->timing.rearm_ms) { decision->armed = true; }
		return false;
	}
	decision->quiet_since = -1;
	if (decision->candidate != (enum speech_label)winner) { decision->count = 0; }
	decision->candidate = (enum speech_label)winner;
	if (decision->count < decision->timing.accept_frames) { ++decision->count; }
	if (!decision->armed || decision->count < decision->timing.accept_frames ||
	    uptime_ms - decision->last_event < decision->timing.min_gap_ms) { return false; }
	decision->armed = false;
	decision->last_event = uptime_ms;
	*event = decision->candidate;
	return true;
}
