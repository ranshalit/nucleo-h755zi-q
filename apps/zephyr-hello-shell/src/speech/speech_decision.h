/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_DECISION_H
#define APP_SPEECH_DECISION_H
#include "speech_service.h"
struct speech_decision {
	uint16_t threshold_permille;
	struct speech_timing timing;
	enum speech_label candidate;
	unsigned int count;
	bool armed;
	int64_t quiet_since;
	int64_t last_event;
	int64_t last_update;
	bool evaluated;
	struct speech_decision_frame frame;
};
void speech_decision_reset(struct speech_decision *decision);
void speech_decision_set_threshold(struct speech_decision *decision, uint16_t threshold_permille);
void speech_decision_set_timing(struct speech_decision *decision, const struct speech_timing *timing);
bool speech_decision_update(struct speech_decision *decision, const uint16_t scores[SPEECH_LABEL_COUNT],
			    int64_t uptime_ms, enum speech_label *event);
void speech_history_record(struct speech_history *history, const struct speech_decision *decision);
#endif
