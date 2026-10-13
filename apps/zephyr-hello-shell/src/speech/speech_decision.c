/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_decision.h"
#include <string.h>
void speech_decision_reset(struct speech_decision *decision)
{
	memset(decision, 0, sizeof(*decision));
	decision->armed = true;
	decision->quiet_since = -1;
	decision->last_event = -1000;
	decision->last_update = -100;
}
bool speech_decision_update(struct speech_decision *decision, const uint16_t scores[4],
			    int64_t uptime_ms, enum speech_label *event)
{
	if (uptime_ms - decision->last_update < 100) { return false; }
	decision->last_update = uptime_ms;
	unsigned int winner = 0;
	bool tie = false;
	for (unsigned int i = 1; i < 4; ++i) {
		if (scores[i] > scores[winner]) { winner = i; tie = false; }
		else if (scores[i] == scores[winner]) { tie = true; }
	}
	bool keyword = !tie && winner >= SPEECH_YES && scores[winner] >= 800;
	if (!keyword) {
		decision->count = 0;
		if (decision->quiet_since < 0) { decision->quiet_since = uptime_ms; }
		if (uptime_ms - decision->quiet_since >= 300) { decision->armed = true; }
		return false;
	}
	decision->quiet_since = -1;
	if (decision->candidate != (enum speech_label)winner) { decision->count = 0; }
	decision->candidate = (enum speech_label)winner;
	if (decision->count < 3) { ++decision->count; }
	if (!decision->armed || decision->count < 3 ||
	    uptime_ms - decision->last_event < 1000) { return false; }
	decision->armed = false;
	decision->last_event = uptime_ms;
	*event = decision->candidate;
	return true;
}
