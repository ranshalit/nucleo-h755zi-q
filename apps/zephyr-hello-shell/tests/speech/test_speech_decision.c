/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "speech_decision.h"
int main(void)
{
	struct speech_decision decision;
	enum speech_label event;
	uint16_t yes[SPEECH_LABEL_COUNT] = {0,50,900,50}, no[SPEECH_LABEL_COUNT] = {0,50,50,900};
	uint16_t quiet[SPEECH_LABEL_COUNT] = {900,50,25,25}, weak[SPEECH_LABEL_COUNT] = {0,300,700,0};
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
	uint16_t tie[SPEECH_LABEL_COUNT] = {0,0,850,850};
	speech_decision_reset(&decision);
	for (int64_t t = 0; t < 1000; t += 100) {
		assert(!speech_decision_update(&decision, tie, t, &event));
	}
	static const enum speech_label directions[] = {SPEECH_UP, SPEECH_DOWN, SPEECH_LEFT, SPEECH_RIGHT};
	for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
		uint16_t scores[SPEECH_LABEL_COUNT] = {0, 50};
		scores[directions[i]] = 900;
		speech_decision_reset(&decision);
		assert(!speech_decision_update(&decision, scores, 0, &event));
		assert(!speech_decision_update(&decision, scores, 100, &event));
		assert(speech_decision_update(&decision, scores, 200, &event));
		assert(event == directions[i]);
		speech_decision_reset(&decision);
		speech_decision_set_threshold(&decision, 950);
		for (int64_t t = 0; t < 500; t += 100) {
			assert(!speech_decision_update(&decision, scores, t, &event));
		}
		speech_decision_set_threshold(&decision, 900);
		assert(!speech_decision_update(&decision, scores, 500, &event));
		assert(!speech_decision_update(&decision, scores, 600, &event));
		assert(speech_decision_update(&decision, scores, 700, &event));
		assert(event == directions[i]);
	}
	uint16_t weak_up[SPEECH_LABEL_COUNT] = {0, 300, 0, 0, 700};
	speech_decision_reset(&decision);
	for (int64_t t = 0; t < 1000; t += 100) {
		assert(!speech_decision_update(&decision, weak_up, t, &event));
	}
	speech_decision_reset(&decision);
	assert(decision.threshold_permille == 800);
	speech_decision_set_threshold(&decision, 900);
	uint16_t below[SPEECH_LABEL_COUNT] = {0,50,899,50};
	uint16_t at_threshold[SPEECH_LABEL_COUNT] = {0,50,900,50};
	for (int64_t t = 0; t < 300; t += 100) {
		assert(!speech_decision_update(&decision, below, t, &event));
	}
	assert(!speech_decision_update(&decision, at_threshold, 300, &event));
	assert(!speech_decision_update(&decision, at_threshold, 400, &event));
	assert(speech_decision_update(&decision, at_threshold, 500, &event));
	assert(event == SPEECH_YES);

	speech_decision_reset(&decision);
	assert(!speech_decision_update(&decision, yes, 0, &event));
	assert(!speech_decision_update(&decision, yes, 100, &event));
	speech_decision_set_threshold(&decision, 900);
	assert(!speech_decision_update(&decision, at_threshold, 200, &event));
	assert(!speech_decision_update(&decision, at_threshold, 300, &event));
	assert(speech_decision_update(&decision, at_threshold, 400, &event));
	assert(event == SPEECH_YES);
	{
		struct speech_decision d;
		enum speech_label ev = SPEECH_SILENCE;
		uint16_t s[SPEECH_LABEL_COUNT] = {0};
		speech_decision_reset(&d);
		s[SPEECH_STOP] = 950;
		bool fired = false;
		for (int64_t t = 0; t <= 400 && !fired; t += 100) {
			fired = speech_decision_update(&d, s, t, &ev);
		}
		assert(fired && ev == SPEECH_STOP);
		puts("PASS: stop is an accepted keyword");
	}
	{
		struct speech_decision d;
		enum speech_label ev;
		uint16_t s[SPEECH_LABEL_COUNT] = {0, 50, 900};
		uint16_t q[SPEECH_LABEL_COUNT] = {900, 50};
		speech_decision_reset(&d);
		assert(d.timing.accept_frames == 3 && d.timing.rearm_ms == 300 && d.timing.min_gap_ms == 1000);
		speech_decision_set_timing(&d, &(struct speech_timing){.accept_frames = 3, .rearm_ms = 300, .min_gap_ms = 1000});
		assert(!speech_decision_update(&d, s, 0, &ev));
		assert(!speech_decision_update(&d, s, 100, &ev));
		assert(speech_decision_update(&d, s, 200, &ev));
		assert(!speech_decision_update(&d, q, 300, &ev));
		assert(!speech_decision_update(&d, q, 500, &ev));
		for (int64_t t = 600; t <= 800; t += 100) {
			assert(!speech_decision_update(&d, s, t, &ev));
		}
		assert(!speech_decision_update(&d, q, 900, &ev));
		assert(!speech_decision_update(&d, q, 1200, &ev));
		assert(!speech_decision_update(&d, s, 1300, &ev));
		assert(!speech_decision_update(&d, s, 1400, &ev));
		assert(speech_decision_update(&d, s, 1500, &ev));
		speech_decision_set_timing(&d, &(struct speech_timing){.accept_frames = 1, .rearm_ms = 0, .min_gap_ms = 0});
		assert(speech_decision_update(&d, s, 1600, &ev) == false);
		assert(!speech_decision_update(&d, q, 1700, &ev));
		assert(speech_decision_update(&d, s, 1800, &ev));
		assert(speech_timing_valid(&(struct speech_timing){1, 0, 0}));
		assert(speech_timing_valid(&(struct speech_timing){10, 2000, 5000}));
		assert(!speech_timing_valid(&(struct speech_timing){0, 100, 500}));
		assert(!speech_timing_valid(&(struct speech_timing){11, 100, 500}));
		assert(!speech_timing_valid(&(struct speech_timing){1, 2001, 500}));
		assert(!speech_timing_valid(&(struct speech_timing){1, 100, 5001}));
		puts("PASS: runtime decision timing");
	}
	puts("PASS: configurable thresholds, cadence, quiet rearm, ties and direction labels");
	return 0;
}
