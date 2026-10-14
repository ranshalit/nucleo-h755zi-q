/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_audio.h"
#include <limits.h>
#include <string.h>

void speech_audio_reset(struct speech_audio_queue *queue, uint32_t session)
{
	queue->head = 0;
	queue->count = 0;
	queue->session = session;
	++queue->generation;
}

size_t speech_audio_push(struct speech_audio_queue *queue, uint32_t session,
			 const uint8_t *pcm, size_t bytes)
{
	if (session != queue->session || !pcm || bytes % 4 || bytes > SPEECH_QUEUE_BYTES) {
		return 0;
	}
	if (bytes > SPEECH_QUEUE_BYTES - queue->count) {
		queue->drops += queue->count / 4;
		speech_audio_reset(queue, session);
	}
	size_t tail = (queue->head + queue->count) % SPEECH_QUEUE_BYTES;
	size_t first = SPEECH_QUEUE_BYTES - tail;
	if (first > bytes) { first = bytes; }
	memcpy(queue->data + tail, pcm, first);
	memcpy(queue->data, pcm + first, bytes - first);
	queue->count += bytes;
	return bytes;
}

size_t speech_audio_read(struct speech_audio_queue *queue, uint32_t session,
			 uint8_t *out, size_t capacity)
{
	if (session != queue->session || !out) { return 0; }
	size_t bytes = capacity - capacity % 4;
	if (bytes > queue->count) { bytes = queue->count; }
	size_t first = SPEECH_QUEUE_BYTES - queue->head;
	if (first > bytes) { first = bytes; }
	memcpy(out, queue->data + queue->head, first);
	memcpy(out + first, queue->data, bytes - first);
	queue->head = (queue->head + bytes) % SPEECH_QUEUE_BYTES;
	queue->count -= bytes;
	return bytes;
}

void speech_decimator_reset(struct speech_decimator *filter)
{
	memset(filter, 0, sizeof(*filter));
}

size_t speech_decimator_process(struct speech_decimator *filter, const uint8_t *pcm,
				size_t bytes, int16_t *out)
{
	/* 63-tap Hamming-windowed sinc, 6 kHz cutoff at 48 kHz; Q15 sum 32768. */
	static const int16_t taps[63] = {
		-19,-29,-23,0,32,55,48,0,-72,-123,-104,0,148,247,204,0,
		-277,-455,-372,0,499,819,674,0,-938,-1598,-1392,0,2411,
		5175,7371,8206,7371,5175,2411,0,-1392,-1598,-938,0,674,
		819,499,0,-372,-455,-277,0,204,247,148,0,-104,-123,-72,
		0,48,55,32,0,-23,-29,-19
	};
	size_t produced = 0;
	for (size_t pos = 0; pos + 4 <= bytes; pos += 4) {
		/* Left channel avoids cancellation if the headset channels differ. */
		filter->history[filter->cursor] = (int16_t)((uint16_t)pcm[pos] |
							  ((uint16_t)pcm[pos + 1] << 8));
		filter->cursor = (filter->cursor + 1) % 63;
		if (++filter->phase != 3) { continue; }
		filter->phase = 0;
		int64_t sum = 0;
		size_t index = filter->cursor;
		for (size_t i = 0; i < 63; ++i) {
			index = index ? index - 1 : 62;
			sum += (int32_t)filter->history[index] * taps[i];
		}
		sum = (sum + 16384) >> 15;
		if (sum > INT16_MAX) { sum = INT16_MAX; }
		if (sum < INT16_MIN) { sum = INT16_MIN; }
		out[produced++] = (int16_t)sum;
	}
	return produced;
}
