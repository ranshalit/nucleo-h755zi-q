/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "speech_service.h"

int main(void)
{
	struct speech_attempt_tracker t;
	uint16_t scores[SPEECH_LABEL_COUNT] = {0};
	speech_attempt_reset(&t);
	scores[SPEECH_LEFT] = 674;
	speech_attempt_update(&t, scores, SPEECH_LEFT, 100, false, SPEECH_UNKNOWN);
	assert(t.live.valid && t.live.peaks[SPEECH_LEFT - SPEECH_YES] == 674);
	scores[SPEECH_SILENCE] = 900;
	scores[SPEECH_LEFT] = 710;
	speech_attempt_update(&t, scores, SPEECH_SILENCE, 200, false, SPEECH_UNKNOWN);
	speech_attempt_update(&t, scores, SPEECH_UNKNOWN, 400, false, SPEECH_UNKNOWN);
	assert(t.live.valid && !t.last.valid);
	speech_attempt_update(&t, scores, SPEECH_SILENCE, 500, false, SPEECH_UNKNOWN);
	assert(!t.live.valid && t.last.valid && t.last.peaks[4] == 710);
	uint32_t previous = t.last.sequence;
	scores[SPEECH_NO] = 950;
	speech_attempt_update(&t, scores, SPEECH_NO, 600, true, SPEECH_NO);
	assert(t.last.sequence == previous && t.live.accepted[1]);
	assert(t.live.peaks[4] == 710);
	scores[SPEECH_LEFT] = 10;
	speech_attempt_update(&t, scores, SPEECH_SILENCE, 700, false, SPEECH_UNKNOWN);
	scores[SPEECH_UP] = 960;
	speech_attempt_update(&t, scores, SPEECH_UP, 900, false, SPEECH_UNKNOWN);
	assert(t.live.valid && t.live.peaks[2] == 960 && t.live.peaks[4] == 710);
	assert(t.live.accepted[1] && !t.live.accepted[2]);
	speech_attempt_update(&t, scores, SPEECH_UNKNOWN, 1000, false, SPEECH_UNKNOWN);
	speech_attempt_update(&t, scores, SPEECH_UNKNOWN, 1200, false, SPEECH_UNKNOWN);
	assert(t.live.valid);
	speech_attempt_interrupt(&t, 1250);
	assert(t.last.interrupted && !t.live.valid && t.last.end_ms == 1250);
	previous = t.last.sequence;
	speech_attempt_interrupt(&t, 1300);
	assert(t.last.sequence == previous && t.last.end_ms == 1250);
	scores[SPEECH_NO] = 15;
	scores[SPEECH_UP] = 20;
	speech_attempt_update(&t, scores, SPEECH_LEFT, 1400, false, SPEECH_UNKNOWN);
	assert(t.live.peaks[1] == 15 && !t.live.accepted[1]);
	assert(t.last.interrupted && t.last.sequence == previous);
	speech_attempt_reset(&t);
	assert(!t.live.valid && !t.last.valid);
	speech_attempt_update(&t, scores, SPEECH_SILENCE, 1500, false, SPEECH_UNKNOWN);
	assert(!t.live.valid);
	t.sequence = UINT32_MAX;
	speech_attempt_update(&t, scores, SPEECH_LEFT, 1600, false, SPEECH_UNKNOWN);
	assert(t.live.sequence == 1);
	puts("PASS: below-threshold attempt peaks, trailing silence, competing words, acceptance, reset and interruption");
	return 0;
}
