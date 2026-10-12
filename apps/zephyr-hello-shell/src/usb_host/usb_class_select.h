/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_CLASS_SELECT_H_
#define USB_CLASS_SELECT_H_

#include <stddef.h>
#include <stdint.h>

#include "usb_audio_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

enum usb_function_kind {
	USB_FUNCTION_NONE = 0,
	USB_FUNCTION_BOOT_MOUSE,
	USB_FUNCTION_AUDIO_IN,
};

struct usb_function_selection {
	enum usb_function_kind kind;
	uint8_t interface_number;
	uint8_t endpoint;
	/* Endpoint wMaxPacketSize and bInterval of the selected function. */
	uint16_t packet_size;
	uint8_t interval;
	/* Valid only for USB_FUNCTION_AUDIO_IN. */
	struct usb_audio_format audio;
};

/*
 * Pick the supported function of a raw configuration descriptor by
 * scanning every interface, not just the first one. A UAC1 microphone
 * input (usb_audio_select_input) wins over a HID boot mouse; speaker
 * OUT-only audio and non-boot HID are never selected.
 *
 * Returns 0 and fills *out, -EINVAL for malformed input (out->kind is
 * left NONE) or -ENOTSUP when nothing supported is present.
 */
int usb_function_select(const uint8_t *raw, size_t length,
			struct usb_function_selection *out);

#ifdef __cplusplus
}
#endif

#endif /* USB_CLASS_SELECT_H_ */
