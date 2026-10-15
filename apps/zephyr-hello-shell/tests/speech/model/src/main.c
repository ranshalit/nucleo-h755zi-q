/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include "speech_model.h"
#include "speech_service.h"
static const uint8_t yes_audio[] = {
#include <yes_1000ms.inc>
};
static const uint8_t no_audio[] = {
#include <no_1000ms.inc>
};
static const uint8_t silence_audio[] = {
#include <silence_1000ms.inc>
};
static const uint8_t noise_audio[] = {
#include <noise_1000ms.inc>
};
static const uint8_t up_audio[] = {
#include <up_1000ms.inc>
};
static const uint8_t down_audio[] = {
#include <down_1000ms.inc>
};
static const uint8_t left_audio[] = {
#include <left_1000ms.inc>
};
static const uint8_t right_audio[] = {
#include <right_1000ms.inc>
};
static const uint8_t stop_audio[] = {
#include <stop_1000ms.inc>
};
static const uint8_t yes_feature[] = {
#include <yes_30ms.inc>
};
static const uint8_t no_feature[] = {
#include <no_30ms.inc>
};

static void feed_wav(const uint8_t *wav, size_t bytes)
{
	assert(memcmp(wav, "RIFF", 4) == 0);
	size_t offset = 12;
	while (offset + 8 <= bytes) {
		uint32_t n = sys_get_le32(wav + offset + 4);
		assert(n <= bytes - offset - 8);
		if (memcmp(wav + offset, "data", 4) == 0) {
			const uint8_t *pcm = wav + offset + 8;
			int16_t samples[320];
			for (size_t pos = 0; pos < n / 2;) {
				size_t count = MIN(320, n / 2 - pos);
				for (size_t i = 0; i < count; ++i) {
					samples[i] = (int16_t)sys_get_le16(pcm + (pos + i) * 2);
				}
				assert(speech_model_push_16k(samples, count) == 0);
				pos += count;
			}
			return;
		}
		offset += 8 + n + (n & 1);
	}
	assert(false);
}

static void classify(const uint8_t *wav, size_t n, int expected)
{
	uint16_t scores[SPEECH_MODEL_LABELS];
	uint32_t us;
	assert(speech_model_reset() == 0);
	feed_wav(wav, n);
	assert(speech_model_window_ready());
	assert(speech_model_invoke(scores, &us) == 0);
	int winner = 0;
	for (int i = 1; i < SPEECH_MODEL_LABELS; ++i) {
		if (scores[i] > scores[winner]) { winner = i; }
	}
	printf("fixture expected=%d winner=%d scores=", expected, winner);
	for (int i = 0; i < SPEECH_MODEL_LABELS; ++i) {
		printf("%s%u", i ? "," : "", scores[i]);
	}
	printf(" us=%u\n", us);
	assert(winner == expected);
}

/* Near-digital-zero input (peak 7/32767) is outside the training data's quiet
 * background-noise silence; it must only never reach the keyword trigger. */
static void assert_no_trigger(const uint8_t *wav, size_t n)
{
	uint16_t scores[SPEECH_MODEL_LABELS];
	uint32_t us;
	assert(speech_model_reset() == 0);
	feed_wav(wav, n);
	assert(speech_model_invoke(scores, &us) == 0);
	printf("fixture near-zero silence scores=");
	for (int i = 0; i < SPEECH_MODEL_LABELS; ++i) {
		printf("%s%u", i ? "," : "", scores[i]);
		if (i >= SPEECH_YES) { assert(scores[i] < 800); }
	}
	printf("\n");
}

static void test_service(void)
{
	struct speech_snapshot snapshot;
	struct usb_audio_snapshot capture = {
		.connected = true, .ready = true, .session = 1,
		.sample_rate_hz = 48000, .channels = 2, .bits_per_sample = 16
	};
	assert(speech_service_start() == 0);
	k_msleep(100);
	assert(speech_service_set_threshold(65) == 0);
	assert(speech_service_set_threshold(49) == -ERANGE);
	speech_capture_changed(&capture);
	k_msleep(30);
	speech_snapshot_get(&snapshot);
	assert(snapshot.state == SPEECH_WARMING_UP && !snapshot.prediction_valid);
	assert(snapshot.threshold_percent == 65);
	uint8_t pcm[192] = {0};
	for (int i = 0; i < 1200; ++i) {
		speech_capture_push(&capture, pcm, sizeof(pcm));
		k_msleep(1);
	}
	k_msleep(100);
	speech_snapshot_get(&snapshot);
	assert(snapshot.state == SPEECH_RUNNING && snapshot.prediction_valid);
	assert(snapshot.windows > 0 && snapshot.drops == 0);
	/* Feed a real keyword until a candidate starts, then interrupt it. */
	size_t offset = 12;
	while (offset + 8 <= sizeof(yes_audio) && memcmp(yes_audio + offset, "data", 4)) {
		uint32_t n = sys_get_le32(yes_audio + offset + 4);
		offset += 8 + n + (n & 1);
	}
	assert(offset + 8 <= sizeof(yes_audio));
	uint32_t audio_bytes = sys_get_le32(yes_audio + offset + 4);
	assert(audio_bytes <= sizeof(yes_audio) - offset - 8);
	bool live = false;
	for (size_t pos = 0; pos + 16 <= audio_bytes; pos += 16) {
		for (size_t sample = 0; sample < 8; ++sample) {
			uint16_t value = sys_get_le16(yes_audio + offset + 8 + pos + sample * 2);
			for (size_t repeat = 0; repeat < 3; ++repeat) {
				size_t dst = (sample * 3 + repeat) * 4;
				sys_put_le16(value, pcm + dst);
				sys_put_le16(value, pcm + dst + 2);
			}
		}
		speech_capture_push(&capture, pcm, 96);
		k_usleep(500);
		speech_snapshot_get(&snapshot);
		if (snapshot.attempts.live.valid) { live = true; break; }
	}
	assert(live);
	++capture.missed;
	speech_capture_changed(&capture);
	speech_snapshot_get(&snapshot);
	assert(!snapshot.prediction_valid && !snapshot.event_valid && snapshot.gaps == 1);
	assert(snapshot.threshold_percent == 65);
	assert(!snapshot.attempts.live.valid && snapshot.attempts.last.valid);
	assert(snapshot.attempts.last.interrupted);
	capture.ready = false;
	capture.connected = false;
	++capture.session;
	speech_capture_changed(&capture);
	speech_snapshot_get(&snapshot);
	assert(snapshot.state == SPEECH_UNAVAILABLE && !snapshot.prediction_valid);
	assert(!snapshot.attempts.live.valid && !snapshot.attempts.last.valid);
	capture.ready = true;
	capture.connected = true;
	capture.sample_rate_hz = 44100;
	speech_capture_changed(&capture);
	speech_snapshot_get(&snapshot);
	assert(snapshot.state == SPEECH_UNSUPPORTED_FORMAT);
	assert(snapshot.threshold_percent == 65);
	assert(speech_service_set_threshold(80) == 0);
	puts("PASS: worker warmup/inference/gap/disconnect/unsupported transitions");
}

int main(void)
{
	static const int8_t yes_expected[40] = {
		124,105,126,103,125,101,123,100,116,98,115,97,113,90,91,82,
		104,96,117,97,121,103,126,101,125,104,126,104,125,101,116,90,
		81,74,80,71,83,76,82,71
	};
	static const int8_t no_expected[40] = {
		126,103,124,102,124,102,123,100,118,97,118,100,118,98,121,100,
		121,98,117,91,96,74,54,87,100,87,109,92,91,80,64,55,83,74,
		74,78,114,95,101,81
	};
	int8_t feature[40];
	assert(speech_model_init() == 0);
	feed_wav(yes_feature, sizeof(yes_feature));
	assert(speech_model_latest_feature(feature) == 0);
	assert(memcmp(feature, yes_expected, 40) == 0);
	assert(!speech_model_window_ready());
	assert(speech_model_reset() == 0);
	feed_wav(no_feature, sizeof(no_feature));
	assert(speech_model_latest_feature(feature) == 0);
	assert(memcmp(feature, no_expected, 40) == 0);
	classify(yes_audio, sizeof(yes_audio), SPEECH_YES);
	classify(no_audio, sizeof(no_audio), SPEECH_NO);
	classify(up_audio, sizeof(up_audio), SPEECH_UP);
	classify(down_audio, sizeof(down_audio), SPEECH_DOWN);
	classify(left_audio, sizeof(left_audio), SPEECH_LEFT);
	classify(right_audio, sizeof(right_audio), SPEECH_RIGHT);
	classify(stop_audio, sizeof(stop_audio), SPEECH_STOP);
	assert_no_trigger(silence_audio, sizeof(silence_audio));
	classify(noise_audio, sizeof(noise_audio), SPEECH_SILENCE);
	/* Reset must reproduce the initial frontend state. */
	classify(yes_audio, sizeof(yes_audio), SPEECH_YES);
	printf("PASS: model fixtures; arenas frontend=%zu classifier=%zu\n",
	       speech_model_arena_used(true), speech_model_arena_used(false));
	test_service();
	return 0;
}
