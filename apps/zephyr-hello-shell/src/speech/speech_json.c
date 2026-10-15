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

static int emit_attempt(char *out, size_t capacity, size_t *offset,
			const struct speech_attempt *attempt, bool live,
			const char *const labels[])
{
	if (!attempt->valid) { return append(out, capacity, offset, "null"); }
	if (!attempt->sequence || attempt->start_ms < 0 ||
	    (live && (attempt->interrupted || attempt->end_ms != 0)) ||
	    (!live && attempt->end_ms < attempt->start_ms)) {
		return -EINVAL;
	}
	for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
		if (attempt->peaks[i] > 1000) { return -EINVAL; }
	}
	int ret;
#define PART(...) do { ret = append(out, capacity, offset, __VA_ARGS__); if (ret) { return ret; } } while (0)
	PART("{\"sequence\":%u,\"start_ms\":%lld,\"end_ms\":",
	     attempt->sequence, (long long)attempt->start_ms);
	if (live) { PART("null"); }
	else { PART("%lld", (long long)attempt->end_ms); }
	PART(",\"interrupted\":%s,\"keywords\":[", attempt->interrupted ? "true" : "false");
	for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
		PART("%s{\"label\":\"%s\",\"peak\":%u.%03u,\"accepted\":%s}",
		     i ? "," : "", labels[SPEECH_YES + i],
		     attempt->peaks[i] / 1000, attempt->peaks[i] % 1000,
		     attempt->accepted[i] ? "true" : "false");
	}
	PART("]}");
#undef PART
	return 0;
}

int speech_snapshot_json(const struct speech_snapshot *snapshot, char *out, size_t capacity)
{
	static const char *const states[] = {
		"unavailable", "initializing", "warming-up", "running", "unsupported-format", "error"
	};
	static const char *const labels[SPEECH_LABEL_COUNT] = {
		"silence", "unknown", "yes", "no", "up", "down", "left", "right", "stop"
	};
	if (!snapshot || !out || !snapshot->reason ||
	    (unsigned)snapshot->state >= 6 ||
	    snapshot->threshold_percent < 50 || snapshot->threshold_percent > 100 ||
	    !speech_timing_valid(&snapshot->timing) ||
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
	EMIT(",\"threshold_percent\":%u,\"accept_frames\":%u,\"rearm_ms\":%u,\"min_gap_ms\":%u,\"inference_us\":%u,\"processing_us\":%u,\"max_processing_us\":%u,"
	     "\"windows\":%u,\"gaps\":%u,\"drops\":%u,\"session\":%u",
	     snapshot->threshold_percent, snapshot->timing.accept_frames,
	     snapshot->timing.rearm_ms, snapshot->timing.min_gap_ms, snapshot->inference_us, snapshot->processing_us,
	     snapshot->max_processing_us,
	     snapshot->windows, snapshot->gaps, snapshot->drops, snapshot->session);
	EMIT(",\"live_attempt\":");
	ret = emit_attempt(out, capacity, &offset, &snapshot->attempts.live, true, labels);
	if (ret) { return ret; }
	EMIT(",\"last_attempt\":");
	ret = emit_attempt(out, capacity, &offset, &snapshot->attempts.last, false, labels);
	if (ret) { return ret; }
	EMIT("}");
#undef EMIT
	return (int)offset;
}
