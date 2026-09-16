/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define USB_HOST_POLL_THREAD_STACK_SIZE 1024
#define USB_HOST_POLL_THREAD_PRIORITY 7
#define USB_HOST_POLL_PERIOD_MS 20

K_THREAD_STACK_DEFINE(usb_host_poll_stack, USB_HOST_POLL_THREAD_STACK_SIZE);
static struct k_thread usb_host_poll_thread;
static int usb_host_init_result = -EAGAIN;

static void usb_host_poll_loop(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		stm32_hcd_adapter_poll();
		k_msleep(USB_HOST_POLL_PERIOD_MS);
	}
}

int board_usb_host_start(void)
{
	usb_host_init_result = stm32_hcd_adapter_init();
	if (usb_host_init_result != 0) {
		return usb_host_init_result;
	}

	k_tid_t tid = k_thread_create(&usb_host_poll_thread, usb_host_poll_stack,
				      K_THREAD_STACK_SIZEOF(usb_host_poll_stack),
				      usb_host_poll_loop, NULL, NULL, NULL,
				      USB_HOST_POLL_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "usb_host_poll");

	return 0;
}

static int cmd_usbhost_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (usb_host_init_result != 0) {
		shell_error(shell, "usb host init failed: %d", usb_host_init_result);
		return usb_host_init_result;
	}

	struct usb_enumerator *e = stm32_hcd_adapter_enumerator();

	shell_print(shell, "state: %s", usb_enum_state_name(e->state));
	shell_print(shell, "error: %s", usb_enum_error_name(e->error));
	return 0;
}

static int cmd_usbhost_descriptor(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct usb_enumerator *e = stm32_hcd_adapter_enumerator();

	if (e->state != USB_ENUM_DESCRIBED) {
		shell_error(shell, "no described device (state: %s)",
			    usb_enum_state_name(e->state));
		return -ENODEV;
	}

	shell_print(shell, "vendor_id:    0x%04x", e->descriptor.vendor_id);
	shell_print(shell, "product_id:   0x%04x", e->descriptor.product_id);
	shell_print(shell, "device_class: 0x%02x", e->descriptor.device_class);
	shell_print(shell, "usb_version:  0x%04x", e->descriptor.usb_version);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_usbhost,
	SHELL_CMD(status, NULL, "Show USB host enumeration state", cmd_usbhost_status),
	SHELL_CMD(descriptor, NULL, "Show enumerated device descriptor",
		  cmd_usbhost_descriptor),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usbhost, &sub_usbhost, "USB host status commands", NULL);
