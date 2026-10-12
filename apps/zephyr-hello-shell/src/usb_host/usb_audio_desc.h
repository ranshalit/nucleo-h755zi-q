/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_AUDIO_DESC_H_
#define USB_AUDIO_DESC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Audio input stream selected from a raw USB configuration descriptor.
 * packet_bytes is the endpoint wMaxPacketSize (receive buffer size per
 * 1 ms full-speed frame); it is always >= the nominal payload of
 * sample_rate_hz * channels * subframe bytes / 1000, rounded up.
 */
struct usb_audio_format {
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t endpoint;
	uint8_t channels;
	uint8_t bits_per_sample;
	uint8_t interval;
	uint16_t packet_bytes;
	uint32_t sample_rate_hz;
	uint16_t audio_class_bcd;
};

/* Preferred rate when the device advertises it. */
#define USB_AUDIO_PREFERRED_RATE_HZ 48000U

/*
 * Walk a full configuration descriptor (raw bytes as returned by
 * GET_DESCRIPTOR(CONFIGURATION)) and select a USB Audio Class 1.0
 * audio-streaming isochronous IN alternate setting carrying Type I
 * integer PCM.
 *
 * Returns 0 and fills *out on success; *out is untouched on failure.
 * Returns -EINVAL for malformed descriptors or bad arguments and
 * -ENOTSUP when the descriptors are well formed but no supported
 * audio input format exists.
 */
int usb_audio_select_input(const uint8_t *raw, size_t length,
			   struct usb_audio_format *out);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_DESC_H_ */
