/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_AUDIO_H
#define APP_SPEECH_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#define SPEECH_QUEUE_BYTES 48000U
struct speech_audio_queue {
	uint8_t data[SPEECH_QUEUE_BYTES];
	size_t head;
	size_t count;
	uint32_t session;
	uint32_t generation;
	uint32_t drops;
};
struct speech_decimator {
	int16_t history[63];
	size_t cursor;
	unsigned int phase;
};
void speech_audio_reset(struct speech_audio_queue *queue, uint32_t session);
size_t speech_audio_push(struct speech_audio_queue *queue, uint32_t session,
			 const uint8_t *pcm, size_t bytes);
size_t speech_audio_read(struct speech_audio_queue *queue, uint32_t session,
			 uint8_t *out, size_t capacity);
void speech_decimator_reset(struct speech_decimator *filter);
size_t speech_decimator_process(struct speech_decimator *filter, const uint8_t *pcm,
				size_t bytes, int16_t *out);
#endif
