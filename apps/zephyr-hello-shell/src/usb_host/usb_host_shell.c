/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"
#include "usb_gpio_capture.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "usbh_core.h"

#define USB_HOST_POLL_THREAD_STACK_SIZE 2048
#define USB_HOST_POLL_THREAD_PRIORITY 7
#define USB_HOST_POLL_PERIOD_MS 1

K_THREAD_STACK_DEFINE(usb_host_poll_stack, USB_HOST_POLL_THREAD_STACK_SIZE);
static struct k_thread usb_host_poll_thread;
static atomic_t usb_host_poll_paused = ATOMIC_INIT(1);
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
		if (!atomic_get(&usb_host_poll_paused)) {
			(void)USBH_Process(&usb_host);
			if (usb_host.gState == HOST_CHECK_CLASS &&
			    usb_host.ClassNumber == 0U) {
				printf("ST USBH: enumeration complete; no class driver registered\n");
				usb_host.gState = HOST_ABORT_STATE;
			}
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

static void print_capture_result(const struct shell *shell)
{
	extern uint32_t SystemCoreClock;
	const uint32_t *buf;
	uint32_t count;
	uint32_t cycles;
	uint32_t prev_dp, prev_dm;
	uint32_t transitions = 0;

	usb_gpio_capture_get(&buf, &count, &cycles);

	if (count == 0U) {
		shell_error(shell, "no samples captured");
		return;
	}

	shell_print(shell, "samples: %u, cycles: %u, core_clock_hz: %u",
		    count, cycles, SystemCoreClock);
	if (SystemCoreClock != 0U) {
		shell_print(shell, "avg_ns_per_sample: %u",
			    (uint32_t)(((uint64_t)cycles * 1000000000ULL) /
				       ((uint64_t)count * SystemCoreClock)));
	}

	/* D-=bit11, D+=bit12. Print only J/K/SE0 transitions to keep the
	 * dump compact; a long run with zero transitions means no
	 * electrical activity was seen on D+/D- during the window.
	 */
	prev_dp = (buf[0] >> 12) & 1U;
	prev_dm = (buf[0] >> 11) & 1U;
	shell_print(shell, "idx=0 DP=%u DM=%u (initial)", prev_dp, prev_dm);
	for (uint32_t i = 1; i < count; i++) {
		uint32_t dp = (buf[i] >> 12) & 1U;
		uint32_t dm = (buf[i] >> 11) & 1U;

		if (dp != prev_dp || dm != prev_dm) {
			shell_print(shell, "idx=%u DP=%u DM=%u", i, dp, dm);
			prev_dp = dp;
			prev_dm = dm;
			transitions++;
		}
	}
	shell_print(shell, "total_transitions: %u", transitions);
}

static int cmd_usbhost_capture(const struct shell *shell, size_t argc,
				char **argv)
{
	uint32_t num_samples = USB_GPIO_CAPTURE_MAX_SAMPLES;

	if (argc > 1) {
		num_samples = (uint32_t)atoi(argv[1]);
	}

	usb_gpio_capture_run(num_samples);
	print_capture_result(shell);
	return 0;
}

static int cmd_usbhost_arm(const struct shell *shell, size_t argc, char **argv)
{
	int timeout_ms = 3000;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    "armed: capture will trigger on next SETUP submission "
		    "(timeout %d ms)", timeout_ms);
	usb_gpio_capture_arm();

	while (usb_gpio_capture_armed() && timeout_ms > 0) {
		k_sleep(K_MSEC(10));
		timeout_ms -= 10;
	}

	if (usb_gpio_capture_armed()) {
		usb_gpio_capture_disarm();
		shell_error(shell, "timed out waiting for a SETUP submission");
		return -ETIMEDOUT;
	}

	print_capture_result(shell);
	return 0;
}

static int cmd_usbhost_rawtest(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/*
	 * Hypothesis (usb_debug.md Step 31): the USBH middleware's
	 * background poll thread keeps retrying channel 0 against the same
	 * device concurrently with this test's channel 1. Pause it here so
	 * the raw test has the bus to itself, matching the bare-metal A/B
	 * test's conditions exactly (nothing else driving the core).
	 *
	 * The poll thread now starts paused by default (see
	 * usb_host_poll_paused's initializer), so if this command is the
	 * first USB host activity since boot, the core has never seen a
	 * channel-0 transaction at all - matching bare-metal even more
	 * closely than just pausing mid-stream. Use `usbhost resume` first
	 * if you want the normal enumeration flow running before this test.
	 */
	atomic_set(&usb_host_poll_paused, 1);
	k_msleep(5); /* let any in-flight USBH_Process() iteration finish */

	shell_print(shell,
		    "running bare-metal HAL HC_Init/StartXfer control test "
		    "(bypasses USBH middleware, middleware polling paused); "
		    "watch console for 'rawtest:' lines");
	ret = stm32_hcd_adapter_raw_control_test();

	atomic_set(&usb_host_poll_paused, 0);
	return ret;
}

static int cmd_usbhost_pause(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	atomic_set(&usb_host_poll_paused, 1);
	shell_print(shell, "usbh middleware polling paused");
	return 0;
}

static int cmd_usbhost_resume(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	atomic_set(&usb_host_poll_paused, 0);
	shell_print(shell, "usbh middleware polling resumed");
	return 0;
}

static int cmd_usbhost_vbuscycle(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t hold_ms = 2000;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/*
	 * Pause the middleware poll thread during the cycle so it doesn't
	 * fight over PD10/HPRT state while we force a real power-off/on,
	 * matching the isolation approach used by `usbhost rawtest`.
	 */
	atomic_set(&usb_host_poll_paused, 1);
	k_msleep(5);

	shell_print(shell, "hard power-cycling VBUS for %u ms; watch console",
		    hold_ms);
	stm32_hcd_adapter_vbus_cycle(hold_ms);

	atomic_set(&usb_host_poll_paused, 0);
	return 0;
}

static int cmd_usbhost_gpio(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/*
	 * Dump the live GPIOA config for PA11 (D-)/PA12 (D+) directly from
	 * silicon, to check whether the pins are actually still configured
	 * as AF10 push-pull (as our adapter_init_hardware() sets them) at
	 * the moment a D+/D- capture shows zero electrical activity, or
	 * whether something else (e.g. Zephyr's own pinctrl/gpio subsystem)
	 * has reconfigured them since boot.
	 */
	uint32_t moder = GPIOA->MODER;
	uint32_t afrh = GPIOA->AFR[1]; /* AF for pins 8-15 */
	uint32_t otyper = GPIOA->OTYPER;
	uint32_t ospeedr = GPIOA->OSPEEDR;
	uint32_t pupdr = GPIOA->PUPDR;
	uint32_t idr = GPIOA->IDR;

	shell_print(shell, "GPIOA base=0x%08lx", (unsigned long)GPIOA);
	shell_print(shell, "MODER=0x%08lx (PA11=%lu PA12=%lu; 00=in 01=out 10=AF 11=analog)",
		    (unsigned long)moder,
		    (unsigned long)((moder >> (11 * 2)) & 0x3U),
		    (unsigned long)((moder >> (12 * 2)) & 0x3U));
	shell_print(shell, "AFRH=0x%08lx (PA11 AF=%lu PA12 AF=%lu; want 10=AF10)",
		    (unsigned long)afrh,
		    (unsigned long)((afrh >> ((11 - 8) * 4)) & 0xFU),
		    (unsigned long)((afrh >> ((12 - 8) * 4)) & 0xFU));
	shell_print(shell, "OTYPER=0x%08lx (PA11=%lu PA12=%lu; 0=push-pull)",
		    (unsigned long)otyper,
		    (unsigned long)((otyper >> 11) & 0x1U),
		    (unsigned long)((otyper >> 12) & 0x1U));
	shell_print(shell, "OSPEEDR=0x%08lx (PA11=%lu PA12=%lu; want 11=very high)",
		    (unsigned long)ospeedr,
		    (unsigned long)((ospeedr >> (11 * 2)) & 0x3U),
		    (unsigned long)((ospeedr >> (12 * 2)) & 0x3U));
	shell_print(shell, "PUPDR=0x%08lx (PA11=%lu PA12=%lu; want 00=no pull)",
		    (unsigned long)pupdr,
		    (unsigned long)((pupdr >> (11 * 2)) & 0x3U),
		    (unsigned long)((pupdr >> (12 * 2)) & 0x3U));
	shell_print(shell, "IDR=0x%08lx (PA11(D-)=%lu PA12(D+)=%lu, live now)",
		    (unsigned long)idr,
		    (unsigned long)((idr >> 11) & 0x1U),
		    (unsigned long)((idr >> 12) & 0x1U));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_usbhost,
	SHELL_CMD(status, NULL, "Show ST USB host state", cmd_usbhost_status),
	SHELL_CMD(descriptor, NULL, "Show enumerated device descriptor",
		  cmd_usbhost_descriptor),
	SHELL_CMD(capture, NULL,
		  "Tight-loop capture of D+/D- GPIO state (usbhost capture [n])",
		  cmd_usbhost_capture),
	SHELL_CMD(arm, NULL,
		  "Arm a capture synchronized to the next SETUP submission",
		  cmd_usbhost_arm),
	SHELL_CMD(rawtest, NULL,
		  "Bare-metal HAL-only control transfer test, bypassing USBH middleware",
		  cmd_usbhost_rawtest),
	SHELL_CMD(pause, NULL,
		  "Pause the USBH middleware's background poll thread",
		  cmd_usbhost_pause),
	SHELL_CMD(resume, NULL,
		  "Resume the USBH middleware's background poll thread",
		  cmd_usbhost_resume),
	SHELL_CMD(vbuscycle, NULL,
		  "Hard power-cycle VBUS (PD10 off/on) to recover a wedged device",
		  cmd_usbhost_vbuscycle),
	SHELL_CMD(gpio, NULL,
		  "Dump live PA11/PA12 GPIO mode/AF/speed/pull registers",
		  cmd_usbhost_gpio),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usbhost, &sub_usbhost, "USB host status commands", NULL);
