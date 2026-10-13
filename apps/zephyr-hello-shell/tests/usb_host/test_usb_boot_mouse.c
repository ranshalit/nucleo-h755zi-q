/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usb_boot_mouse.h"
#include "usb_class_select.h"

#define H570E_FIXTURE \
	"apps/zephyr-hello-shell/tests/usb_host/fixtures/h570e_config.hex"

static uint8_t h570e[1024];
static size_t h570e_len;

static void load_h570e(void)
{
	FILE *f = fopen(H570E_FIXTURE, "r");
	unsigned int byte;

	if (f == NULL) {
		fprintf(stderr, "cannot open fixture %s (run from worktree root)\n",
			H570E_FIXTURE);
		exit(1);
	}
	while (fscanf(f, "%x", &byte) == 1) {
		assert(h570e_len < sizeof(h570e) && byte <= 0xffU);
		h570e[h570e_len++] = (uint8_t)byte;
	}
	fclose(f);
	assert(h570e_len == 291U);
}

/* Synthetic boot mouse: one HID boot/mouse interface, EP 0x81 interrupt IN. */
static const uint8_t boot_mouse_config[] = {
	0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xa0, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x34, 0x00,
	0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x0a,
};

/* Composite: vendor interface 0 first, boot mouse on interface 1 (EP 0x82). */
static const uint8_t composite_mouse_config[] = {
	0x09, 0x02, 0x32, 0x00, 0x02, 0x01, 0x00, 0xa0, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x34, 0x00,
	0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x04,
};

static void test_selection(void)
{
	struct usb_function_selection sel;
	uint8_t work[1024];

	memset(&sel, 0xa5, sizeof(sel));
	assert(usb_function_select(boot_mouse_config, sizeof(boot_mouse_config),
				   &sel) == 0);
	assert(sel.kind == USB_FUNCTION_BOOT_MOUSE);
	assert(sel.interface_number == 0U && sel.endpoint == 0x81U);
	assert(sel.packet_size == 4U && sel.interval == 10U);

	assert(usb_function_select(composite_mouse_config,
				   sizeof(composite_mouse_config), &sel) == 0);
	assert(sel.kind == USB_FUNCTION_BOOT_MOUSE);
	assert(sel.interface_number == 1U && sel.endpoint == 0x82U);
	assert(sel.packet_size == 8U && sel.interval == 4U);

	load_h570e();
	assert(usb_function_select(h570e, h570e_len, &sel) == 0);
	assert(sel.kind == USB_FUNCTION_AUDIO_IN);
	assert(sel.interface_number == 1U && sel.endpoint == 0x81U);
	assert(sel.audio.interface_number == 1U);
	assert(sel.audio.alternate_setting == 1U);
	assert(sel.audio.endpoint == 0x81U);
	assert(sel.audio.channels == 2U && sel.audio.bits_per_sample == 16U);
	assert(sel.audio.sample_rate_hz == 48000U);
	assert(sel.audio.packet_bytes == 200U && sel.audio.interval == 1U);

	/*
	 * Turn the mic endpoint into an OUT endpoint: only the speaker
	 * direction remains, and HID IF3 is not a boot mouse, so nothing
	 * may be selected.
	 */
	memcpy(work, h570e, h570e_len);
	for (size_t i = 0; i + 3U < h570e_len; i++) {
		if (work[i] == 0x09U && work[i + 1U] == 0x05U &&
		    work[i + 2U] == 0x81U) {
			work[i + 2U] = 0x03U;
		}
	}
	memset(&sel, 0, sizeof(sel));
	assert(usb_function_select(work, h570e_len, &sel) == -ENOTSUP);
	assert(sel.kind == USB_FUNCTION_NONE);

	/* HID boot keyboard (protocol 1) is not a mouse. */
	memcpy(work, boot_mouse_config, sizeof(boot_mouse_config));
	work[16] = 0x01U;
	assert(usb_function_select(work, sizeof(boot_mouse_config), &sel) == -ENOTSUP);

	/* Boot mouse whose only endpoint is OUT is rejected. */
	memcpy(work, boot_mouse_config, sizeof(boot_mouse_config));
	work[29] = 0x01U;
	assert(usb_function_select(work, sizeof(boot_mouse_config), &sel) == -ENOTSUP);

	/* Truncated / malformed descriptors are rejected. */
	assert(usb_function_select(boot_mouse_config,
				   sizeof(boot_mouse_config) - 1U, &sel) == -EINVAL);
	memcpy(work, boot_mouse_config, sizeof(boot_mouse_config));
	work[27] = 0x00U;
	assert(usb_function_select(work, sizeof(boot_mouse_config), &sel) == -EINVAL);
	assert(usb_function_select(NULL, 34U, &sel) == -EINVAL);
	assert(usb_function_select(boot_mouse_config, sizeof(boot_mouse_config),
				   NULL) == -EINVAL);
	puts("PASS: class selection");
}

int main(void)
{
	test_selection();

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
