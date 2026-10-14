/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "speech_audio.h"
static struct speech_audio_queue queue;
static uint8_t packet[192];
static uint8_t pcm[48000 * 4];
static int16_t whole[16000], chunks[16000];

static double tone(unsigned int hz)
{
	for (size_t i = 0; i < 48000; ++i) {
		int16_t value = (int16_t)(12000 * sin(2 * 3.141592653589793 * hz * i / 48000));
		pcm[i * 4] = (uint16_t)value & 255;
		pcm[i * 4 + 1] = (uint16_t)value >> 8;
		value = -value;
		pcm[i * 4 + 2] = (uint16_t)value & 255;
		pcm[i * 4 + 3] = (uint16_t)value >> 8;
	}
	struct speech_decimator filter;
	speech_decimator_reset(&filter);
	assert(speech_decimator_process(&filter, pcm, sizeof(pcm), whole) == 16000);
	double energy = 0;
	for (size_t i = 100; i < 16000; ++i) { energy += (double)whole[i] * whole[i]; }
	return sqrt(energy / 15900);
}

int main(void)
{
	uint8_t out[192];
	memset(packet, 123, sizeof(packet));
	speech_audio_reset(&queue, 1);
	assert(speech_audio_push(&queue, 1, packet, sizeof(packet)) == sizeof(packet));
	assert(speech_audio_push(&queue, 1, packet, 3) == 0);
	assert(speech_audio_read(&queue, 1, out, 191) == 188);
	assert(memcmp(out, packet, 188) == 0);
	speech_audio_reset(&queue, 2);
	assert(speech_audio_read(&queue, 1, out, sizeof(out)) == 0);
	assert(speech_audio_read(&queue, 2, out, sizeof(out)) == 0);
	assert(speech_audio_push(&queue, 1, packet, sizeof(packet)) == 0);
	for (size_t i = 0; i < 250; ++i) {
		assert(speech_audio_push(&queue, 2, packet, sizeof(packet)) == sizeof(packet));
	}
	uint32_t generation = queue.generation;
	assert(speech_audio_push(&queue, 2, packet, sizeof(packet)) == sizeof(packet));
	assert(queue.drops == 12000 && queue.generation != generation);
	assert(queue.count == sizeof(packet));
	double pass = tone(1000);
	assert(fabs(20 * log10(pass / (12000 / sqrt(2)))) < 1.0);
	struct speech_decimator filter;
	speech_decimator_reset(&filter);
	size_t produced = 0;
	for (size_t i = 0; i < sizeof(pcm); i += 188) {
		size_t count = sizeof(pcm) - i;
		if (count > 188) { count = 188; }
		produced += speech_decimator_process(&filter, pcm + i, count, chunks + produced);
	}
	assert(produced == 16000 && memcmp(whole, chunks, sizeof(whole)) == 0);
	double stop = tone(12000);
	assert(20 * log10(stop / (12000 / sqrt(2))) < -30);
	printf("PASS: audio queue, reset, overflow, chunk continuity; stop/pass %.1f dB\n",
	       20 * log10(stop / pass));
	return 0;
}
