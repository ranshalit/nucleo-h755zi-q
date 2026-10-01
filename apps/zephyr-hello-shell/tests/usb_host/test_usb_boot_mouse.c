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
	puts("PASS: boot mouse reports");
	return 0;
}
