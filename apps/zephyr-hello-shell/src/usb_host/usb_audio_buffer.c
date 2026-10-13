/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_audio_buffer.h"

#include <string.h>

void usb_audio_ring_init(struct usb_audio_ring *ring, const char *reason)
{
	ring->capacity = 0U;
	ring->head = 0U;
	ring->count = 0U;
	ring->frame_bytes = 0U;
	memset(&ring->status, 0, sizeof(ring->status));
	ring->status.reason = reason;
}

static void ring_clear(struct usb_audio_ring *ring)
{
	ring->head = 0U;
	ring->count = 0U;
	ring->status.level = 0U;
	ring->status.packets = 0U;
	ring->status.drops = 0U;
	ring->status.errors = 0U;
	ring->status.missed = 0U;
}

uint32_t usb_audio_ring_attach(struct usb_audio_ring *ring, uint32_t sample_rate_hz,
			       uint8_t channels, uint8_t bits_per_sample)
{
	if (sample_rate_hz == 0U || channels == 0U || channels > 8U ||
	    bits_per_sample < 16U || bits_per_sample > 32U ||
	    (bits_per_sample % 8U) != 0U) {
		return 0U;
	}

	uint16_t frame = (uint16_t)(channels * (bits_per_sample / 8U));

	ring->frame_bytes = frame;
	ring->capacity = (sizeof(ring->data) / frame) * frame;
	ring_clear(ring);
	ring->status.session++;
	if (ring->status.session == 0U) {
		ring->status.session = 1U;
	}
	ring->status.connected = true;
	ring->status.ready = false;
	ring->status.sample_rate_hz = sample_rate_hz;
	ring->status.channels = channels;
	ring->status.bits_per_sample = bits_per_sample;
	ring->status.reason = "configuring";
	return ring->status.session;
}

void usb_audio_ring_set_ready(struct usb_audio_ring *ring, bool ready,
			      const char *reason)
{
	ring->status.ready = ready && ring->status.connected;
	ring->status.reason = reason;
	if (!ring->status.ready) {
		ring->head = 0U;
		ring->count = 0U;
		ring->status.level = 0U;
	}
}

void usb_audio_ring_detach(struct usb_audio_ring *ring, const char *reason)
{
	ring->head = 0U;
	ring->count = 0U;
	ring->status.session++;
	ring->status.connected = false;
	ring->status.ready = false;
	ring->status.level = 0U;
	ring->status.reason = reason;
}

static uint16_t packet_peak(const struct usb_audio_ring *ring,
			    const uint8_t *packet, size_t length)
{
	size_t bytes = ring->status.bits_per_sample / 8U;
	uint32_t peak = 0U;

	for (size_t off = 0U; off + bytes <= length; off += bytes) {
		/* Little-endian signed sample; keep its top 16 bits. */
		int16_t sample = (int16_t)(packet[off + bytes - 2U] |
					   (packet[off + bytes - 1U] << 8));
		uint32_t mag = sample < 0 ? (uint32_t)(-(int32_t)sample) :
					    (uint32_t)sample;

		if (mag > peak) {
			peak = mag;
		}
	}
	return peak > 32767U ? 32767U : (uint16_t)peak;
}

static uint16_t decay_level(uint16_t level, uint16_t peak)
{
	uint16_t next = (uint16_t)(level - (level + 7U) / 8U);

	return next > peak ? next : peak;
}

size_t usb_audio_ring_write(struct usb_audio_ring *ring, const uint8_t *packet,
			    size_t length)
{
	if (!ring->status.ready) {
		return 0U;
	}
	if ((packet == NULL && length != 0U) || (length % ring->frame_bytes) != 0U) {
		ring->status.errors++;
		return 0U;
	}

	uint16_t peak = packet_peak(ring, packet, length);
	uint16_t level = ring->status.level;

	ring->status.level = peak >= level ? peak :
			     (uint16_t)decay_level(level, peak);
	ring->status.packets++;
	if (length == 0U) {
		return 0U;
	}

	const uint8_t *src = packet;
	size_t n = length;

	if (n > ring->capacity) {
		ring->status.drops += (uint32_t)((n - ring->capacity) / ring->frame_bytes);
		src += n - ring->capacity;
		n = ring->capacity;
	}
	if (ring->count + n > ring->capacity) {
		size_t drop = ring->count + n - ring->capacity;

		ring->head = (ring->head + drop) % ring->capacity;
		ring->count -= drop;
		ring->status.drops += (uint32_t)(drop / ring->frame_bytes);
	}

	size_t tail = (ring->head + ring->count) % ring->capacity;
	size_t first = ring->capacity - tail;

	if (first > n) {
		first = n;
	}
	memcpy(&ring->data[tail], src, first);
	memcpy(ring->data, src + first, n - first);
	ring->count += n;
	return length;
}

size_t usb_audio_ring_read(struct usb_audio_ring *ring, uint32_t session,
			   uint8_t *out, size_t capacity)
{
	if (out == NULL || !ring->status.connected || !ring->status.ready ||
	    session != ring->status.session) {
		return 0U;
	}

	size_t n = capacity - (capacity % ring->frame_bytes);

	if (n > ring->count) {
		n = ring->count;
	}

	size_t first = ring->capacity - ring->head;

	if (first > n) {
		first = n;
	}
	memcpy(out, &ring->data[ring->head], first);
	memcpy(out + first, ring->data, n - first);
	if (n != 0U) {
		ring->head = (ring->head + n) % ring->capacity;
		ring->count -= n;
	}
	return n;
}

size_t usb_audio_ring_capacity(const struct usb_audio_ring *ring)
{
	return ring->capacity;
}

void usb_audio_ring_note_error(struct usb_audio_ring *ring)
{
	ring->status.errors++;
}

void usb_audio_ring_note_missed(struct usb_audio_ring *ring)
{
	ring->status.missed++;
}

void usb_audio_ring_snapshot(const struct usb_audio_ring *ring,
			     struct usb_audio_snapshot *out)
{
	*out = ring->status;
}
