/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

#include <errno.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "usbh_core.h"

#define USB_HOST_POLL_THREAD_STACK_SIZE 2048
#define USB_HOST_POLL_THREAD_PRIORITY 7
#define USB_HOST_POLL_PERIOD_MS 1

K_THREAD_STACK_DEFINE(usb_host_poll_stack, USB_HOST_POLL_THREAD_STACK_SIZE);
static struct k_thread usb_host_poll_thread;
static USBH_HandleTypeDef usb_host;
static int usb_host_init_result = -EAGAIN;

static const char *host_state_name(HOST_StateTypeDef state)
{
	switch (state) {
	case HOST_IDLE: return "idle";
	case HOST_DEV_WAIT_FOR_ATTACHMENT: return "wait_for_attachment";
	case HOST_DEV_ATTACHED: return "attached";
	case HOST_DEV_DISCONNECTED: return "disconnected";
	case HOST_DETECT_DEVICE_SPEED: return "detect_speed";
	case HOST_ENUMERATION: return "enumeration";
	case HOST_CLASS_REQUEST: return "class_request";
	case HOST_INPUT: return "input";
	case HOST_SET_CONFIGURATION: return "set_configuration";
	case HOST_SET_WAKEUP_FEATURE: return "set_wakeup";
	case HOST_CHECK_CLASS: return "check_class";
	case HOST_CLASS: return "class";
	case HOST_SUSPENDED: return "suspended";
	case HOST_ABORT_STATE: return "abort";
	default: return "unknown";
	}
}

static const char *enum_state_name(ENUM_StateTypeDef state)
{
	switch (state) {
	case ENUM_IDLE: return "idle";
	case ENUM_GET_FULL_DEV_DESC: return "get_full_device_descriptor";
	case ENUM_SET_ADDR: return "set_address";
	case ENUM_GET_CFG_DESC: return "get_configuration_descriptor";
	case ENUM_GET_FULL_CFG_DESC: return "get_full_configuration_descriptor";
	case ENUM_GET_MFC_STRING_DESC: return "get_manufacturer";
	case ENUM_GET_PRODUCT_STRING_DESC: return "get_product";
	case ENUM_GET_SERIALNUM_STRING_DESC: return "get_serial";
	default: return "unknown";
	}
}

static const char *control_state_name(CTRL_StateTypeDef state)
{
	switch (state) {
	case CTRL_IDLE: return "idle";
	case CTRL_SETUP: return "setup";
	case CTRL_SETUP_WAIT: return "setup_wait";
	case CTRL_DATA_IN: return "data_in";
	case CTRL_DATA_IN_WAIT: return "data_in_wait";
	case CTRL_DATA_OUT: return "data_out";
	case CTRL_DATA_OUT_WAIT: return "data_out_wait";
	case CTRL_STATUS_IN: return "status_in";
	case CTRL_STATUS_IN_WAIT: return "status_in_wait";
	case CTRL_STATUS_OUT: return "status_out";
	case CTRL_STATUS_OUT_WAIT: return "status_out_wait";
	case CTRL_ERROR: return "error";
	case CTRL_STALLED: return "stalled";
	case CTRL_COMPLETE: return "complete";
	default: return "unknown";
	}
}

static void usb_host_user_process(USBH_HandleTypeDef *phost, uint8_t id)
{
	ARG_UNUSED(phost);

	switch (id) {
	case HOST_USER_CONNECTION:
		printf("ST USBH: device connected\n");
		break;
	case HOST_USER_DISCONNECTION:
		printf("ST USBH: device disconnected\n");
		break;
	case HOST_USER_UNRECOVERED_ERROR:
		printf("ST USBH: unrecovered error\n");
		break;
	default:
		break;
	}
}

static void usb_host_poll_loop(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)USBH_Process(&usb_host);
		if (usb_host.gState == HOST_CHECK_CLASS &&
		    usb_host.ClassNumber == 0U) {
			printf("ST USBH: enumeration complete; no class driver registered\n");
			usb_host.gState = HOST_ABORT_STATE;
		}
		k_msleep(USB_HOST_POLL_PERIOD_MS);
	}
}

int board_usb_host_start(void)
{
	if (USBH_Init(&usb_host, usb_host_user_process, 0U) != USBH_OK ||
	    usb_host.pData == NULL) {
		usb_host_init_result = -EIO;
		return usb_host_init_result;
	}
	if (USBH_Start(&usb_host) != USBH_OK) {
		usb_host_init_result = -EIO;
		return usb_host_init_result;
	}

	k_tid_t tid = k_thread_create(&usb_host_poll_thread, usb_host_poll_stack,
				      K_THREAD_STACK_SIZEOF(usb_host_poll_stack),
				      usb_host_poll_loop, NULL, NULL, NULL,
				      USB_HOST_POLL_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "stm32_usbh");
	usb_host_init_result = 0;
	return 0;
}

static int cmd_usbhost_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "stack: ST stm32-mw-usb-host Core");
	shell_print(shell, "PA10_IDR: %d", stm32_hcd_adapter_id_pin_level());
	shell_print(shell, "CIDSTS: %d", stm32_hcd_adapter_id_status());
	shell_print(shell, "HPRT: 0x%08x", stm32_hcd_adapter_port_status());

	if (usb_host_init_result != 0) {
		shell_error(shell, "usb host init failed: %d", usb_host_init_result);
		return usb_host_init_result;
	}

	shell_print(shell, "state: %s", host_state_name(usb_host.gState));
	shell_print(shell, "enum_state: %s", enum_state_name(usb_host.EnumState));
	shell_print(shell, "control_state: %s",
		    control_state_name(usb_host.Control.state));
	shell_print(shell, "connected: %u", usb_host.device.is_connected);
	shell_print(shell, "port_enabled: %u", usb_host.device.PortEnabled);
	return 0;
}

static int cmd_usbhost_descriptor(const struct shell *shell, size_t argc,
				  char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (usb_host.EnumState < ENUM_SET_ADDR &&
	    usb_host.gState == HOST_ENUMERATION) {
		shell_error(shell, "full device descriptor not received");
		return -ENODEV;
	}
	if (usb_host.device.DevDesc.bLength != USB_LEN_DEV_DESC) {
		shell_error(shell, "no valid device descriptor");
		return -ENODEV;
	}

	shell_print(shell, "vendor_id:    0x%04x",
		    usb_host.device.DevDesc.idVendor);
	shell_print(shell, "product_id:   0x%04x",
		    usb_host.device.DevDesc.idProduct);
	shell_print(shell, "device_class: 0x%02x",
		    usb_host.device.DevDesc.bDeviceClass);
	shell_print(shell, "usb_version:  0x%04x",
		    usb_host.device.DevDesc.bcdUSB);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_usbhost,
	SHELL_CMD(status, NULL, "Show ST USB host state", cmd_usbhost_status),
	SHELL_CMD(descriptor, NULL, "Show enumerated device descriptor",
		  cmd_usbhost_descriptor),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usbhost, &sub_usbhost, "USB host status commands", NULL);
