/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_SERVICE_H
#define APP_SPEECH_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "speech_attempt.h"
#include "usb_audio_buffer.h"
enum speech_state {
	SPEECH_UNAVAILABLE, SPEECH_INITIALIZING, SPEECH_WARMING_UP,
	SPEECH_RUNNING, SPEECH_UNSUPPORTED_FORMAT, SPEECH_ERROR
};
enum speech_label {
	SPEECH_SILENCE, SPEECH_UNKNOWN, SPEECH_YES, SPEECH_NO,
	SPEECH_UP, SPEECH_DOWN, SPEECH_LEFT, SPEECH_RIGHT, SPEECH_STOP,
	SPEECH_LABEL_COUNT
};
#define SPEECH_WORDS "yes/no/up/down/left/right/stop"
#define SPEECH_THRESHOLD_DEFAULT_PERCENT 60
#define SPEECH_ACCEPT_FRAMES_MAX 10
#define SPEECH_REARM_MS_MAX 2000
#define SPEECH_MIN_GAP_MS_MAX 5000
/* Keyword acceptance: frames above threshold, quiet time to re-arm, gap between events. */
struct speech_timing {
	uint8_t accept_frames;
	uint16_t rearm_ms;
	uint16_t min_gap_ms;
};
#define SPEECH_TIMING_DEFAULT { .accept_frames = 2, .rearm_ms = 300, .min_gap_ms = 1000 }
#define SPEECH_HISTORY_CAPACITY 20
#define SPEECH_JSON_CAPACITY 12288
enum speech_decision_outcome {
	SPEECH_DECISION_BELOW, SPEECH_DECISION_TIE, SPEECH_DECISION_FRAMES,
	SPEECH_DECISION_REARM, SPEECH_DECISION_GAP, SPEECH_DECISION_ACCEPTED
};
struct speech_decision_frame {
	int64_t uptime_ms;
	uint32_t sequence;
	enum speech_label label;
	uint16_t score_permille;
	uint16_t threshold_permille;
	uint16_t gap_remaining_ms;
	uint8_t count;
	uint8_t required;
	bool armed;
	enum speech_decision_outcome outcome;
};
struct speech_history {
	struct speech_decision_frame frames[SPEECH_HISTORY_CAPACITY];
	uint32_t sequence;
	uint8_t count;
	uint8_t next;
};
static inline bool speech_timing_valid(const struct speech_timing *timing)
{
	return timing && timing->accept_frames >= 1 &&
	       timing->accept_frames <= SPEECH_ACCEPT_FRAMES_MAX &&
	       timing->rearm_ms <= SPEECH_REARM_MS_MAX &&
	       timing->min_gap_ms <= SPEECH_MIN_GAP_MS_MAX;
}
struct speech_snapshot {
	enum speech_state state;
	const char *reason;
	bool prediction_valid;
	enum speech_label label;
	uint16_t score_permille;
	bool event_valid;
	enum speech_label event_label;
	uint32_t event_sequence;
	int64_t event_uptime_ms;
	uint32_t inference_us;
	uint32_t processing_us;
	uint32_t max_processing_us;
	uint32_t windows;
	uint32_t gaps;
	uint32_t drops;
	uint32_t session;
	uint8_t threshold_percent;
	struct speech_timing timing;
	struct speech_attempt_tracker attempts;
	struct speech_history history;
};
int speech_service_start(void);
int speech_service_set_threshold(uint8_t threshold_percent);
int speech_service_set_timing(const struct speech_timing *timing);
void speech_capture_push(const struct usb_audio_snapshot *capture,
			 const uint8_t *pcm, size_t bytes);
void speech_capture_changed(const struct usb_audio_snapshot *capture);
void speech_snapshot_get(struct speech_snapshot *out);
int speech_snapshot_json(const struct speech_snapshot *snapshot, char *out, size_t capacity);
#endif
