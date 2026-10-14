/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_SERVICE_H
#define APP_SPEECH_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "usb_audio_buffer.h"
enum speech_state {
	SPEECH_UNAVAILABLE, SPEECH_INITIALIZING, SPEECH_WARMING_UP,
	SPEECH_RUNNING, SPEECH_UNSUPPORTED_FORMAT, SPEECH_ERROR
};
enum speech_label { SPEECH_SILENCE, SPEECH_UNKNOWN, SPEECH_YES, SPEECH_NO };
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
};
int speech_service_start(void);
void speech_capture_push(const struct usb_audio_snapshot *capture,
			 const uint8_t *pcm, size_t bytes);
void speech_capture_changed(const struct usb_audio_snapshot *capture);
void speech_snapshot_get(struct speech_snapshot *out);
int speech_snapshot_json(const struct speech_snapshot *snapshot, char *out, size_t capacity);
#endif
