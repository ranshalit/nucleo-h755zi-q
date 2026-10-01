/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "usb_boot_mouse.h"

int main(void)
{
	struct usb_boot_mouse_report report = {0};
	const uint8_t movement[] = {0x05, 0xfd, 0x07};
	const uint8_t wheel[] = {0x01, 0x00, 0xff, 0xfe};

	assert(usb_boot_mouse_decode(movement, sizeof(movement), &report));
	assert(report.buttons == 0x05);
	assert(report.x == -3 && report.y == 7 && report.wheel == 0);
	assert(usb_boot_mouse_decode(wheel, sizeof(wheel), &report));
	assert(report.buttons == 1 && report.x == 0 && report.y == -1);
	assert(report.wheel == -2);
	assert(!usb_boot_mouse_decode(movement, 2, &report));
	assert(!usb_boot_mouse_decode(NULL, 3, &report));
	assert(!usb_boot_mouse_decode(movement, 3, NULL));
	struct usb_boot_mouse_snapshot snapshot = {0};
	const struct usb_boot_mouse_report click = { .buttons = 1, .x = 5, .y = -3,
						    .wheel = 1 };

	usb_boot_mouse_accumulate(&snapshot, &click);
	usb_boot_mouse_accumulate(&snapshot, &click);
	assert(snapshot.reports == 2 && snapshot.x == 10 && snapshot.y == -6);
	assert(snapshot.wheel == 2 && snapshot.presses[0] == 1);
	const struct usb_boot_mouse_report release = { .buttons = 0 };

	usb_boot_mouse_accumulate(&snapshot, &release);
	usb_boot_mouse_accumulate(&snapshot, &click);
	assert(snapshot.presses[0] == 2 && snapshot.reports == 4);
	puts("PASS: boot mouse reports");
	return 0;
}
