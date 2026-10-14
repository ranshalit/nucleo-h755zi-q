/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_DECISION_H
#define APP_SPEECH_DECISION_H
#include "speech_service.h"
struct speech_decision {
	enum speech_label candidate;
	unsigned int count;
	bool armed;
	int64_t quiet_since;
	int64_t last_event;
	int64_t last_update;
};
void speech_decision_reset(struct speech_decision *decision);
bool speech_decision_update(struct speech_decision *decision, const uint16_t scores[4],
			    int64_t uptime_ms, enum speech_label *event);
#endif
