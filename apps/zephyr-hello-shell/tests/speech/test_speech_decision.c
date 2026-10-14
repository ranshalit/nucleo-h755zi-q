/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "speech_decision.h"
int main(void)
{
	struct speech_decision decision;
	enum speech_label event;
	uint16_t yes[4] = {0,50,900,50}, no[4] = {0,50,50,900};
	uint16_t quiet[4] = {900,50,25,25}, weak[4] = {0,300,700,0};
	speech_decision_reset(&decision);
	assert(!speech_decision_update(&decision, weak, 0, &event));
	assert(!speech_decision_update(&decision, yes, 100, &event));
	assert(!speech_decision_update(&decision, yes, 150, &event));
	assert(!speech_decision_update(&decision, yes, 200, &event));
	assert(speech_decision_update(&decision, yes, 300, &event));
	assert(event == SPEECH_YES);
	for (int64_t t = 400; t < 2000; t += 100) {
		assert(!speech_decision_update(&decision, yes, t, &event));
	}
	for (int64_t t = 2000; t <= 2300; t += 100) {
		assert(!speech_decision_update(&decision, quiet, t, &event));
	}
	assert(!speech_decision_update(&decision, no, 2400, &event));
	assert(!speech_decision_update(&decision, no, 2500, &event));
	assert(speech_decision_update(&decision, no, 2600, &event));
	assert(event == SPEECH_NO);
	speech_decision_reset(&decision);
	assert(!speech_decision_update(&decision, yes, 2700, &event));
	assert(!speech_decision_update(&decision, yes, 2800, &event));
	assert(speech_decision_update(&decision, yes, 2900, &event));
	uint16_t tie[4] = {0,0,850,850};
	speech_decision_reset(&decision);
	for (int64_t t = 0; t < 1000; t += 100) {
		assert(!speech_decision_update(&decision, tie, t, &event));
	}
	puts("PASS: decision thresholds, cadence, quiet rearm, ties and reset");
	return 0;
}
