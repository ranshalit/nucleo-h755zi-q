/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "speech_service.h"
int main(void)
{
	char buffer[SPEECH_JSON_CAPACITY];
	struct speech_snapshot snapshot = {
		.state = SPEECH_UNAVAILABLE, .reason = "mic \"gone\"\n", .threshold_percent = 80,
		.timing = SPEECH_TIMING_DEFAULT
	};
	int n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0 && (size_t)n == strlen(buffer));
	assert(strstr(buffer, "\"label\":null,\"score\":null"));
	assert(strstr(buffer, "\"last_event\":null"));
	assert(strstr(buffer, "\"history\":[]"));
	assert(strstr(buffer, "\"threshold_percent\":80"));
	assert(strstr(buffer, "\"live_attempt\":null,\"last_attempt\":null"));
	assert(strstr(buffer, "mic \\\"gone\\\"\\u000a"));
	assert(speech_snapshot_json(&snapshot, buffer, 10) == -ENOSPC);
	snapshot.state = SPEECH_RUNNING;
	snapshot.reason = "recognizing English yes/no";
	snapshot.prediction_valid = true;
	snapshot.label = SPEECH_YES;
	snapshot.score_permille = 910;
	snapshot.event_valid = true;
	snapshot.event_label = SPEECH_NO;
	snapshot.event_sequence = 7;
	snapshot.event_uptime_ms = INT64_C(5000000000);
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0);
	assert(strstr(buffer, "\"label\":\"yes\",\"score\":0.910"));
	assert(strstr(buffer, "\"last_event\":{\"label\":\"no\",\"sequence\":7,\"uptime_ms\":5000000000}"));
	snapshot.threshold_percent = 65;
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0 && strstr(buffer, "\"threshold_percent\":65"));
	snapshot.timing = (struct speech_timing){.accept_frames = 2, .rearm_ms = 150, .min_gap_ms = 700};
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0 && strstr(buffer, "\"threshold_percent\":65,\"accept_frames\":2,\"rearm_ms\":150,\"min_gap_ms\":700"));
	puts(buffer);
	static const char *const names[] = {"up", "down", "left", "right", "stop"};
	for (int i = 0; i < 5; ++i) {
		char expected[64];
		snapshot.label = (enum speech_label)(SPEECH_UP + i);
		snapshot.event_label = snapshot.label;
		assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) > 0);
		snprintf(expected, sizeof(expected), "\"label\":\"%s\",\"score\"", names[i]);
		assert(strstr(buffer, expected));
		snprintf(expected, sizeof(expected), "\"last_event\":{\"label\":\"%s\"", names[i]);
		assert(strstr(buffer, expected));
	}
	snapshot.attempts.live = (struct speech_attempt){
		.valid = true, .sequence = 1, .start_ms = 100,
		.peaks = {674, 0, 0, 0, 0, 0, 0}
	};
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) > 0);
	assert(strstr(buffer, "\"end_ms\":null"));
	assert(strstr(buffer, "\"label\":\"yes\",\"peak\":0.674,\"accepted\":false"));
	snapshot.attempts.last = snapshot.attempts.live;
	snapshot.attempts.last.end_ms = INT64_C(5000000000);
	snapshot.attempts.last.interrupted = true;
	snapshot.attempts.last.accepted[0] = true;
	snapshot.attempts.last.peaks[6] = 1000;
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0);
	assert(strstr(buffer, "\"end_ms\":5000000000,\"interrupted\":true"));
	assert(strstr(buffer, "\"label\":\"yes\",\"peak\":0.674,\"accepted\":true"));
	assert(strstr(buffer, "\"label\":\"stop\",\"peak\":1.000"));
	assert(speech_snapshot_json(&snapshot, buffer, (size_t)n + 1) == n);
	assert(speech_snapshot_json(&snapshot, buffer, (size_t)n) == -ENOSPC);
	snapshot.attempts.live.peaks[0] = 1001;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.attempts.live.peaks[0] = 674;
	snapshot.attempts.live.interrupted = true;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.attempts.live.interrupted = false;
	snapshot.attempts.last.end_ms = 99;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.attempts.last.end_ms = 500;
	snapshot.attempts.live.sequence = 0;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.attempts.live.sequence = UINT32_MAX;
	snapshot.attempts.last.sequence = UINT32_MAX;
	snapshot.attempts.live.start_ms = INT64_MAX;
	snapshot.attempts.last.start_ms = INT64_MAX;
	snapshot.attempts.last.end_ms = INT64_MAX;
	snapshot.windows = snapshot.gaps = snapshot.drops = snapshot.session = UINT32_MAX;
	snapshot.event_sequence = UINT32_MAX;
	snapshot.event_uptime_ms = INT64_MAX;
	snapshot.inference_us = snapshot.processing_us = snapshot.max_processing_us = UINT32_MAX;
	for (size_t i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
		snapshot.attempts.live.peaks[i] = snapshot.attempts.last.peaks[i] = 1000;
	}
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) > 0);
	snapshot.history.count = SPEECH_HISTORY_CAPACITY;
	snapshot.history.next = 3;
	for (unsigned int i = 0; i < SPEECH_HISTORY_CAPACITY; ++i) {
		snapshot.history.frames[(3 + i) % SPEECH_HISTORY_CAPACITY] =
			(struct speech_decision_frame){
				.sequence = UINT32_MAX - SPEECH_HISTORY_CAPACITY + i,
				.uptime_ms = INT64_MAX, .label = SPEECH_UP,
				.score_permille = i ? 840 : 895, .threshold_permille = 800,
				.count = 2, .required = 2, .armed = false,
				.gap_remaining_ms = 5000, .outcome = SPEECH_DECISION_REARM
			};
	}
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0);
	assert(strstr(buffer, "\"label\":\"up\",\"score\":0.895,\"threshold\":0.800"));
	assert(strstr(buffer, "\"count\":2,\"required\":2,\"armed\":false,\"gap_remaining_ms\":5000,\"outcome\":\"rearm\""));
	assert(strstr(buffer, "\"score\":0.895") < strstr(buffer, "\"score\":0.840"));
	assert(speech_snapshot_json(&snapshot, buffer, (size_t)n) == -ENOSPC);
	snapshot.history.frames[3].label = SPEECH_LABEL_COUNT;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.history.frames[3].label = SPEECH_UP;
	snapshot.history.frames[3].outcome = SPEECH_DECISION_ACCEPTED + 1;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.history = (struct speech_history){0};
	snapshot.label = SPEECH_LABEL_COUNT;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.label = SPEECH_YES;
	snapshot.event_label = SPEECH_UNKNOWN;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.event_label = SPEECH_NO;
	snapshot.score_permille = 1001;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.score_permille = 910;
	snapshot.threshold_percent = 49;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	snapshot.threshold_percent = 80;
	snapshot.timing.accept_frames = 0;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	puts("PASS: snapshot JSON attempts, thresholds, nulls, escaping, labels, validation and capacity");
	return 0;
}
