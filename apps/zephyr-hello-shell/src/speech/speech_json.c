/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_service.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

static int append(char *out, size_t capacity, size_t *offset, const char *format, ...)
{
	if (*offset >= capacity) { return -ENOSPC; }
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out + *offset, capacity - *offset, format, args);
	va_end(args);
	if (n < 0) { return -EIO; }
	if ((size_t)n >= capacity - *offset) { return -ENOSPC; }
	*offset += n;
	return 0;
}

int speech_snapshot_json(const struct speech_snapshot *snapshot, char *out, size_t capacity)
{
	static const char *const states[] = {
		"unavailable", "initializing", "warming-up", "running", "unsupported-format", "error"
	};
	static const char *const labels[SPEECH_LABEL_COUNT] = {
		"silence", "unknown", "yes", "no", "up", "down", "left", "right"
	};
	if (!snapshot || !out || !snapshot->reason ||
	    (unsigned)snapshot->state >= 6 ||
	    snapshot->threshold_percent < 50 || snapshot->threshold_percent > 100 ||
	    (snapshot->prediction_valid && ((unsigned)snapshot->label >= SPEECH_LABEL_COUNT || snapshot->score_permille > 1000)) ||
	    (snapshot->event_valid && (snapshot->event_label < SPEECH_YES || snapshot->event_label >= SPEECH_LABEL_COUNT))) {
		return -EINVAL;
	}
	size_t offset = 0;
	int ret;
#define EMIT(...) do { ret = append(out, capacity, &offset, __VA_ARGS__); if (ret) { return ret; } } while (0)
	EMIT("{\"state\":\"%s\",\"reason\":\"", states[snapshot->state]);
	for (const unsigned char *p = (const unsigned char *)snapshot->reason; *p; ++p) {
		if (*p == '"' || *p == '\\') { EMIT("\\%c", *p); }
		else if (*p < 32) { EMIT("\\u%04x", *p); }
		else { EMIT("%c", *p); }
	}
	EMIT("\",\"label\":");
	if (snapshot->prediction_valid) {
		EMIT("\"%s\",\"score\":%u.%03u", labels[snapshot->label],
		     snapshot->score_permille / 1000, snapshot->score_permille % 1000);
	} else { EMIT("null,\"score\":null"); }
	EMIT(",\"last_event\":");
	if (snapshot->event_valid) {
		EMIT("{\"label\":\"%s\",\"sequence\":%u,\"uptime_ms\":%lld}",
		     labels[snapshot->event_label], snapshot->event_sequence,
		     (long long)snapshot->event_uptime_ms);
	} else { EMIT("null"); }
	EMIT(",\"threshold_percent\":%u,\"inference_us\":%u,\"processing_us\":%u,\"max_processing_us\":%u,"
	     "\"windows\":%u,\"gaps\":%u,\"drops\":%u,\"session\":%u}",
	     snapshot->threshold_percent, snapshot->inference_us, snapshot->processing_us,
	     snapshot->max_processing_us,
	     snapshot->windows, snapshot->gaps, snapshot->drops, snapshot->session);
#undef EMIT
	return (int)offset;
}
