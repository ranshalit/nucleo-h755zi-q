/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_boot_mouse.h"

bool usb_boot_mouse_decode(const uint8_t *data, size_t length,
			   struct usb_boot_mouse_report *report)
{
	if (data == NULL || report == NULL || length < 3U) {
		return false;
	}

	report->buttons = data[0];
	report->x = (int8_t)data[1];
	report->y = (int8_t)data[2];
	report->wheel = length >= 4U ? (int8_t)data[3] : 0;
	return true;
}

void usb_boot_mouse_accumulate(struct usb_boot_mouse_snapshot *snapshot,
			       const struct usb_boot_mouse_report *report)
{
	for (uint8_t bit = 0U; bit < 3U; ++bit) {
		uint8_t mask = 1U << bit;

		if ((report->buttons & mask) != 0U && (snapshot->buttons & mask) == 0U) {
			snapshot->presses[bit]++;
		}
	}
	snapshot->buttons = report->buttons;
	snapshot->x += report->x;
	snapshot->y += report->y;
	snapshot->wheel += report->wheel;
	snapshot->reports++;
}
