/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_AUDIO_BUFFER_H_
#define USB_AUDIO_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 250 ms of the measured H570e format: 48 kHz, 2 channels, 16-bit PCM. */
#define USB_AUDIO_RING_MS 250U
#define USB_AUDIO_RING_BYTES (48U * 2U * 2U * USB_AUDIO_RING_MS)

struct usb_audio_snapshot {
	bool connected;
	bool ready;
	uint32_t session;
	uint32_t sample_rate_hz;
	uint8_t channels;
	uint8_t bits_per_sample;
	/* Peak |sample| on a 16-bit scale (0..32767) with ~1 ms/8 decay. */
	uint16_t level;
	/* Isochronous IN packets accepted (including zero-length). */
	uint32_t packets;
	/* Oldest PCM frames discarded because the reader fell behind. */
	uint32_t drops;
	/* Rejected packets and USB transfer errors. */
	uint32_t errors;
	/* Frames in which the host scheduled no completed transfer. */
	uint32_t missed;
	const char *reason;
};

/*
 * Single-producer/single-consumer PCM ring. Not internally locked:
 * callers serialize access (see usb_audio_capture.c).
 */
struct usb_audio_ring {
	uint8_t data[USB_AUDIO_RING_BYTES];
	size_t capacity;
	size_t head;
	size_t count;
	uint16_t frame_bytes;
	struct usb_audio_snapshot status;
};

void usb_audio_ring_init(struct usb_audio_ring *ring, const char *reason);
/* Starts a new session; returns its id, or 0 for an unsupported format. */
uint32_t usb_audio_ring_attach(struct usb_audio_ring *ring, uint32_t sample_rate_hz,
			       uint8_t channels, uint8_t bits_per_sample);
void usb_audio_ring_set_ready(struct usb_audio_ring *ring, bool ready,
			      const char *reason);
/* Ends the session: increments it, discards buffered PCM. */
void usb_audio_ring_detach(struct usb_audio_ring *ring, const char *reason);
/* Appends one packet of whole frames; returns bytes stored. */
size_t usb_audio_ring_write(struct usb_audio_ring *ring, const uint8_t *packet,
			    size_t length);
/* Copies only whole frames of the given session; returns bytes copied. */
size_t usb_audio_ring_read(struct usb_audio_ring *ring, uint32_t session,
			   uint8_t *out, size_t capacity);
size_t usb_audio_ring_capacity(const struct usb_audio_ring *ring);
void usb_audio_ring_note_error(struct usb_audio_ring *ring);
void usb_audio_ring_note_missed(struct usb_audio_ring *ring);
void usb_audio_ring_snapshot(const struct usb_audio_ring *ring,
			     struct usb_audio_snapshot *out);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_BUFFER_H_ */
