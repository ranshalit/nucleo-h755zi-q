/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_audio_capture.h"
#include "usb_boot_mouse.h"

int board_usb_host_start(void)
{
	return -ENOTSUP;
}

void usb_boot_mouse_snapshot_get(struct usb_boot_mouse_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	memset(snapshot, 0, sizeof(*snapshot));
}

void usb_audio_snapshot_get(struct usb_audio_snapshot *out)
{
	if (out == NULL) {
		return;
	}

	memset(out, 0, sizeof(*out));
	out->reason = "usb host unavailable";
}

size_t usb_audio_read(uint32_t session, uint8_t *out, size_t capacity)
{
	(void)session;
	(void)out;
	(void)capacity;
	return 0U;
}
