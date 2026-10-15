/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_ATTEMPT_H
#define APP_SPEECH_ATTEMPT_H
#include <stdbool.h>
#include <stdint.h>
#define SPEECH_KEYWORD_COUNT 7

struct speech_attempt {
	bool valid;
	bool interrupted;
	uint32_t sequence;
	int64_t start_ms;
	int64_t end_ms;
	uint16_t peaks[SPEECH_KEYWORD_COUNT];
	bool accepted[SPEECH_KEYWORD_COUNT];
};

struct speech_attempt_tracker {
	struct speech_attempt live;
	struct speech_attempt last;
	uint32_t sequence;
	int64_t quiet_since;
};

void speech_attempt_reset(struct speech_attempt_tracker *tracker);
void speech_attempt_interrupt(struct speech_attempt_tracker *tracker, int64_t now);
void speech_attempt_update(struct speech_attempt_tracker *tracker,
			   const uint16_t scores[], unsigned int winner,
			   int64_t now, bool accepted, unsigned int event);
#endif
