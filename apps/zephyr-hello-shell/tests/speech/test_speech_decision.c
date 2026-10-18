/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "speech_decision.h"
static void test_defaults(void)
{
	struct speech_decision d;
	enum speech_label event;
	uint16_t scores[SPEECH_LABEL_COUNT] = {0};
	speech_decision_reset(&d);
	assert(d.threshold_permille == 600);
	assert(d.timing.accept_frames == 2);
	assert(d.timing.rearm_ms == 300 && d.timing.min_gap_ms == 1000);
	scores[SPEECH_UP] = 599;
	assert(!speech_decision_update(&d, scores, 0, &event));
	scores[SPEECH_UP] = 600;
	assert(!speech_decision_update(&d, scores, 100, &event));
	assert(speech_decision_update(&d, scores, 200, &event));
	assert(event == SPEECH_UP);
}

static void reset_strict_decision(struct speech_decision *decision)
{
	speech_decision_reset(decision);
	speech_decision_set_threshold(decision, 800);
	speech_decision_set_timing(decision, &(struct speech_timing){3, 300, 1000});
}

static void test_history(void)
{
	struct speech_decision d;
	struct speech_history history = {0};
	enum speech_label event;
	uint16_t scores[SPEECH_LABEL_COUNT] = {0};
	uint16_t quiet[SPEECH_LABEL_COUNT] = {900};
	scores[SPEECH_UP] = 895;
	reset_strict_decision(&d);
	speech_decision_set_timing(&d, &(struct speech_timing){2, 300, 1000});
	assert(!speech_decision_update(&d, scores, 0, &event));
	speech_history_record(&history, &d);
	assert(history.count == 1 && history.frames[0].score_permille == 895);
	assert(history.frames[0].count == 1 && history.frames[0].required == 2);
	assert(history.frames[0].outcome == SPEECH_DECISION_FRAMES);
	assert(!speech_decision_update(&d, scores, 50, &event));
	speech_history_record(&history, &d);
	assert(history.count == 1);
	scores[SPEECH_UP] = 840;
	assert(speech_decision_update(&d, scores, 100, &event));
	speech_history_record(&history, &d);
	assert(history.frames[1].score_permille == 840 && history.frames[1].count == 2);
	assert(history.frames[1].outcome == SPEECH_DECISION_ACCEPTED);
	assert(history.frames[1].armed && history.frames[1].gap_remaining_ms == 0);
	assert(!speech_decision_update(&d, scores, 200, &event));
	speech_history_record(&history, &d);
	assert(!history.frames[2].armed && history.frames[2].gap_remaining_ms == 900);
	assert(history.frames[2].outcome == SPEECH_DECISION_REARM);
	scores[SPEECH_UP] = 700;
	assert(!speech_decision_update(&d, scores, 300, &event));
	speech_history_record(&history, &d);
	assert(history.frames[3].outcome == SPEECH_DECISION_BELOW && history.frames[3].count == 0);
	assert(!speech_decision_update(&d, quiet, 600, &event));
	speech_history_record(&history, &d);
	assert(history.count == 4 && d.armed);
	scores[SPEECH_UP] = 900;
	assert(!speech_decision_update(&d, scores, 700, &event));
	speech_history_record(&history, &d);
	assert(!speech_decision_update(&d, scores, 800, &event));
	speech_history_record(&history, &d);
	assert(history.frames[5].outcome == SPEECH_DECISION_GAP);
	assert(history.frames[5].armed && history.frames[5].gap_remaining_ms == 300);
	scores[SPEECH_DOWN] = 900;
	assert(!speech_decision_update(&d, scores, 900, &event));
	speech_history_record(&history, &d);
	assert(history.frames[6].outcome == SPEECH_DECISION_TIE && history.frames[6].count == 0);
	speech_decision_set_threshold(&d, 950);
	assert(history.frames[0].threshold_permille == 800);
	for (int64_t t = 1000; t < 4000; t += 100) {
		speech_decision_update(&d, scores, t, &event);
		speech_history_record(&history, &d);
	}
	assert(history.count == SPEECH_HISTORY_CAPACITY);
	assert(history.sequence == 37);
	assert(history.frames[history.next].sequence == 18);
	assert(history.frames[(history.next + SPEECH_HISTORY_CAPACITY - 1) %
			      SPEECH_HISTORY_CAPACITY].uptime_ms == 3900);
	history = (struct speech_history){0};
	assert(history.count == 0 && history.sequence == 0);
	reset_strict_decision(&d);
	speech_decision_set_timing(&d, &(struct speech_timing){1, 100, 0});
	scores[SPEECH_DOWN] = 0;
	assert(speech_decision_update(&d, scores, 0, &event));
	scores[SPEECH_UP] = 700;
	assert(!speech_decision_update(&d, scores, 100, &event));
	assert(!d.frame.armed);
	assert(!speech_decision_update(&d, scores, 200, &event));
	assert(d.frame.armed);
	speech_history_record(&history, &d);
	history.sequence = UINT32_MAX;
	speech_history_record(&history, &d);
	assert(history.frames[1].sequence == 1);
	puts("PASS: individual decision scores, frame counts, blockers and bounded history");
}
int main(void)
{
	test_defaults();
	test_history();
	struct speech_decision decision;
	enum speech_label event;
	uint16_t yes[SPEECH_LABEL_COUNT] = {0,50,900,50}, no[SPEECH_LABEL_COUNT] = {0,50,50,900};
	uint16_t quiet[SPEECH_LABEL_COUNT] = {900,50,25,25}, weak[SPEECH_LABEL_COUNT] = {0,300,700,0};
	reset_strict_decision(&decision);
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
	reset_strict_decision(&decision);
	assert(!speech_decision_update(&decision, yes, 2700, &event));
	assert(!speech_decision_update(&decision, yes, 2800, &event));
	assert(speech_decision_update(&decision, yes, 2900, &event));
	uint16_t tie[SPEECH_LABEL_COUNT] = {0,0,850,850};
	reset_strict_decision(&decision);
	for (int64_t t = 0; t < 1000; t += 100) {
		assert(!speech_decision_update(&decision, tie, t, &event));
	}
	static const enum speech_label directions[] = {SPEECH_UP, SPEECH_DOWN, SPEECH_LEFT, SPEECH_RIGHT};
	for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
		uint16_t scores[SPEECH_LABEL_COUNT] = {0, 50};
		scores[directions[i]] = 900;
		reset_strict_decision(&decision);
		assert(!speech_decision_update(&decision, scores, 0, &event));
		assert(!speech_decision_update(&decision, scores, 100, &event));
		assert(speech_decision_update(&decision, scores, 200, &event));
		assert(event == directions[i]);
		reset_strict_decision(&decision);
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
	reset_strict_decision(&decision);
	for (int64_t t = 0; t < 1000; t += 100) {
		assert(!speech_decision_update(&decision, weak_up, t, &event));
	}
	reset_strict_decision(&decision);
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

	reset_strict_decision(&decision);
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
		reset_strict_decision(&d);
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
		reset_strict_decision(&d);
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
