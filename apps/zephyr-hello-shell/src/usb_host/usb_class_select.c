/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_class_select.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define DESC_CONFIGURATION 0x02U
#define DESC_INTERFACE 0x04U
#define DESC_ENDPOINT 0x05U
#define HID_CLASS 0x03U
#define HID_BOOT_SUBCLASS 0x01U
#define HID_MOUSE_PROTOCOL 0x02U
#define EP_TYPE_MASK 0x03U
#define EP_TYPE_INTERRUPT 0x03U
#define MOUSE_MAX_PACKET 64U

static int find_boot_mouse(const uint8_t *raw, uint16_t total,
			   struct usb_function_selection *out)
{
	bool in_mouse = false;
	uint8_t interface_number = 0U;

	for (uint16_t off = raw[0]; off < total;) {
		uint8_t len = raw[off];

		if (len < 2U || (uint32_t)off + len > total) {
			return -EINVAL;
		}
		if (raw[off + 1U] == DESC_INTERFACE) {
			if (len < 9U) {
				return -EINVAL;
			}
			interface_number = raw[off + 2U];
			in_mouse = raw[off + 3U] == 0U &&
				   raw[off + 5U] == HID_CLASS &&
				   raw[off + 6U] == HID_BOOT_SUBCLASS &&
				   raw[off + 7U] == HID_MOUSE_PROTOCOL;
		} else if (raw[off + 1U] == DESC_ENDPOINT && in_mouse) {
			if (len < 7U) {
				return -EINVAL;
			}
			uint8_t address = raw[off + 2U];
			uint16_t mps = (uint16_t)(raw[off + 4U] | (raw[off + 5U] << 8)) &
				       0x7ffU;

			if ((address & 0x80U) != 0U &&
			    (raw[off + 3U] & EP_TYPE_MASK) == EP_TYPE_INTERRUPT &&
			    mps >= 3U && mps <= MOUSE_MAX_PACKET && raw[off + 6U] != 0U) {
				out->kind = USB_FUNCTION_BOOT_MOUSE;
				out->interface_number = interface_number;
				out->endpoint = address;
				out->packet_size = mps;
				out->interval = raw[off + 6U];
				return 0;
			}
		}
		off += len;
	}
	return -ENOTSUP;
}

int usb_function_select(const uint8_t *raw, size_t length,
			struct usb_function_selection *out)
{
	struct usb_function_selection sel;

	if (out == NULL) {
		return -EINVAL;
	}
	out->kind = USB_FUNCTION_NONE;
	if (raw == NULL || length < 9U || raw[0] < 9U ||
	    raw[1] != DESC_CONFIGURATION) {
		return -EINVAL;
	}

	uint16_t total = (uint16_t)(raw[2] | (raw[3] << 8));

	if (total < raw[0] || total > length) {
		return -EINVAL;
	}

	memset(&sel, 0, sizeof(sel));
	int ret = usb_audio_select_input(raw, total, &sel.audio);

	if (ret == 0) {
		sel.kind = USB_FUNCTION_AUDIO_IN;
		sel.interface_number = sel.audio.interface_number;
		sel.endpoint = sel.audio.endpoint;
		sel.packet_size = sel.audio.packet_bytes;
		sel.interval = sel.audio.interval;
	} else if (ret == -EINVAL) {
		return ret;
	} else {
		ret = find_boot_mouse(raw, total, &sel);
		if (ret != 0) {
			return ret;
		}
	}
	*out = sel;
	return 0;
}
