/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_BOOT_MOUSE_H_
#define USB_BOOT_MOUSE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct usb_boot_mouse_report {
	uint8_t buttons;
	int8_t x;
	int8_t y;
	int8_t wheel;
};

struct usb_boot_mouse_snapshot {
	bool connected;
	uint32_t session;
	uint32_t reports;
	uint8_t buttons;
	int64_t x;
	int64_t y;
	int64_t wheel;
	uint32_t presses[3];
};

bool usb_boot_mouse_decode(const uint8_t *data, size_t length,
			   struct usb_boot_mouse_report *report);
void usb_boot_mouse_accumulate(struct usb_boot_mouse_snapshot *snapshot,
			       const struct usb_boot_mouse_report *report);
void usb_boot_mouse_snapshot_get(struct usb_boot_mouse_snapshot *snapshot);
bool usb_boot_mouse_last_report(struct usb_boot_mouse_report *report,
				uint32_t *count);

#endif
