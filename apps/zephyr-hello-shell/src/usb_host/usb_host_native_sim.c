/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

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
