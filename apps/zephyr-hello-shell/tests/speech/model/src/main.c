/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include "speech_model.h"
static const uint8_t yes_audio[] = {
#include <yes_1000ms.inc>
};
static const uint8_t no_audio[] = {
#include <no_1000ms.inc>
};
static const uint8_t silence_audio[] = {
#include <silence_1000ms.inc>
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
	uint16_t scores[4];
	uint32_t us;
	assert(speech_model_reset() == 0);
	feed_wav(wav, n);
	assert(speech_model_window_ready());
	assert(speech_model_invoke(scores, &us) == 0);
	int winner = 0;
	for (int i = 1; i < 4; ++i) {
		if (scores[i] > scores[winner]) { winner = i; }
	}
	printf("fixture expected=%d winner=%d scores=%u,%u,%u,%u us=%u\n",
	       expected, winner, scores[0], scores[1], scores[2], scores[3], us);
	assert(winner == expected);
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
	classify(yes_audio, sizeof(yes_audio), 2);
	classify(no_audio, sizeof(no_audio), 3);
	classify(silence_audio, sizeof(silence_audio), 0);
	/* Reset must reproduce the initial frontend state. */
	classify(yes_audio, sizeof(yes_audio), 2);
	printf("PASS: model fixtures; arenas frontend=%zu classifier=%zu\n",
	       speech_model_arena_used(true), speech_model_arena_used(false));
	return 0;
}
