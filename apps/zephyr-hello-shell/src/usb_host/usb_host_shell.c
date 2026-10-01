/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"
#include "usb_boot_mouse.h"
#include "usb_gpio_capture.h"
#include "../board_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "usbh_core.h"

extern USBH_ClassTypeDef usb_boot_mouse_class;

#define USB_HOST_POLL_THREAD_STACK_SIZE 2048
#define USB_HOST_POLL_THREAD_PRIORITY 10
#define USB_HOST_POLL_PERIOD_MS 1

K_THREAD_STACK_DEFINE(usb_host_poll_stack, USB_HOST_POLL_THREAD_STACK_SIZE);
static struct k_thread usb_host_poll_thread;
static atomic_t usb_host_poll_paused = ATOMIC_INIT(1);
static atomic_t usb_host_poll_busy = ATOMIC_INIT(0);
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

static bool usb_host_needs_fast_poll(void)
{
	switch (usb_host.gState) {
	case HOST_ENUMERATION:
	case HOST_SET_CONFIGURATION:
	case HOST_SET_WAKEUP_FEATURE:
	case HOST_CLASS_REQUEST:
		return true;
	default:
		return usb_host.Control.state != CTRL_IDLE &&
		       usb_host.Control.state != CTRL_COMPLETE;
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
	uint8_t fast_poll_count = 0;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		bool running = !atomic_get(&usb_host_poll_paused);

		if (running) {
			/*
			 * Step 38: in poll mode, service the OTG_FS controller
			 * synchronously right before USBH_Process() instead of
			 * relying on the (now-disabled) async NVIC ISR. This
			 * makes HAL_HCD_IRQHandler() -> USBH_LL_*() callback
			 * dispatch happen deterministically back-to-back with
			 * the USBH state-machine step, in the same thread, on
			 * every loop iteration -- eliminating any possibility
			 * of the ISR preempting USBH_Process() (or vice versa)
			 * mid-state-transition, which bare-metal's superloop
			 * never has to worry about since it never enables this
			 * IRQ in the first place.
			 */
			stm32_hcd_adapter_poll_service();
			(void)USBH_Process(&usb_host);
			if (usb_host.gState == HOST_CHECK_CLASS &&
			    usb_host.ClassNumber == 0U) {
				printf("ST USBH: enumeration complete; no class driver registered\n");
				usb_host.gState = HOST_ABORT_STATE;
			}
		}
		/*
		 * The mouse interrupt-IN transfer needs continuous fast host
		 * processing: a 1 ms class sleep causes TXERR on this board.
		 * Keep this thread below the shell and application workers so
		 * the busy loop cannot prevent them from running.
		 */
		if (running && usb_host.gState == HOST_CLASS) {
			k_busy_wait(50);
		} else if (running && usb_host_needs_fast_poll() &&
		    ++fast_poll_count < 100U) {
			k_busy_wait(50);
		} else {
			fast_poll_count = 0;
			k_msleep(USB_HOST_POLL_PERIOD_MS);
		}
	}
}

int board_usb_host_start(void)
{
	if (USBH_Init(&usb_host, usb_host_user_process, 0U) != USBH_OK ||
	    usb_host.pData == NULL) {
		usb_host_init_result = -EIO;
		return usb_host_init_result;
	}
	if (USBH_RegisterClass(&usb_host, &usb_boot_mouse_class) != USBH_OK) {
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
	shell_print(shell, "cfg_len=%u ctrl_len=%u speed=%u timer=%lu ctrl_timer=%u "
		    "request=%u ep0_mps=%u",
		    usb_host.device.CfgDesc.wTotalLength, usb_host.Control.length,
		    usb_host.device.speed, (unsigned long)usb_host.Timer,
		    usb_host.Control.timer, usb_host.RequestState,
		    usb_host.Control.pipe_size);
	shell_print(shell, "setup: %02x %02x value=%04x index=%04x length=%u",
		    usb_host.Control.setup.b.bmRequestType,
		    usb_host.Control.setup.b.bRequest,
		    usb_host.Control.setup.b.wValue.w,
		    usb_host.Control.setup.b.wIndex.w,
		    usb_host.Control.setup.b.wLength.w);
	stm32_hcd_adapter_debug_channels();
	struct usb_boot_mouse_report report;
	uint32_t count;

	if (usb_boot_mouse_last_report(&report, &count)) {
		shell_print(shell, "mouse: reports=%u buttons=%u dx=%d dy=%d wheel=%d",
			    count, report.buttons, report.x, report.y, report.wheel);
	}
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

static int cmd_usbhost_rawin(const struct shell *shell, size_t argc, char **argv)
{
	const USBH_InterfaceDescTypeDef *interface = &usb_host.device.CfgDesc.Itf_Desc[0];
	const USBH_EpDescTypeDef *endpoint = NULL;
	uint8_t pipe = USBH_MAX_PIPES_NBR;
	bool bulk_schedule = argc == 2U && strcmp(argv[1], "bulk") == 0;

	if (argc > 2U || (argc == 2U && !bulk_schedule)) {
		shell_error(shell, "usage: usbhost rawin [bulk]");
		return -EINVAL;
	}
	if (!atomic_get(&usb_host_poll_paused) ||
	    usb_host.gState != HOST_CLASS ||
	    usb_host.pActiveClass != &usb_boot_mouse_class ||
	    usb_host.device.PortEnabled == 0U) {
		shell_error(shell, "pause an active boot mouse first (usbhost pause)");
		return -EBUSY;
	}
	for (uint8_t i = 0U; i < interface->bNumEndpoints &&
			    i < USBH_MAX_NUM_ENDPOINTS; ++i) {
		if ((interface->Ep_Desc[i].bEndpointAddress & 0x80U) != 0U &&
		    (interface->Ep_Desc[i].bmAttributes & 0x03U) == USB_EP_TYPE_INTR) {
			endpoint = &interface->Ep_Desc[i];
			break;
		}
	}
	if (endpoint == NULL) {
		shell_error(shell, "no mouse interrupt-IN endpoint");
		return -ENODEV;
	}
	for (uint8_t i = 0U; i < USBH_MAX_PIPES_NBR; ++i) {
		if (usb_host.Pipes[i] == (0x8000U | endpoint->bEndpointAddress)) {
			pipe = i;
			break;
		}
	}
	if (pipe == USBH_MAX_PIPES_NBR) {
		shell_error(shell, "mouse interrupt pipe not allocated");
		return -ENODEV;
	}
	k_msleep(5);
	shell_print(shell, "raw HAL interrupt-IN probe; polling remains paused; reset to restore class");
	return stm32_hcd_adapter_raw_mouse_in(pipe, usb_host.device.address,
					      usb_host.device.speed,
					      endpoint->bEndpointAddress,
					      endpoint->wMaxPacketSize,
					      endpoint->bInterval, bulk_schedule);
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

	/*
	 * Experiment: the LED blink threads (400ms/700ms periods) run
	 * continuously and were found to correlate with intermittent
	 * TXERR/hang results in repeated back-to-back rawtest runs -
	 * shifting the inter-command delay shifted *which* run failed,
	 * proving a timing-phase collision rather than random hardware
	 * flakiness. Suspend them for the duration of this test to see if
	 * that alone restores 100% reliability.
	 */
	board_blink_threads_suspend();

	shell_print(shell,
		    "running bare-metal HAL HC_Init/StartXfer control test "
		    "(bypasses USBH middleware, middleware polling paused); "
		    "watch console for 'rawtest:' lines");
	ret = stm32_hcd_adapter_raw_control_test();

	board_blink_threads_resume();
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

static int cmd_usbhost_pollmode(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(shell, "poll mode: %s",
			    stm32_hcd_adapter_get_poll_mode() ? "on" : "off");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		stm32_hcd_adapter_set_poll_mode(true);
	} else if (strcmp(argv[1], "off") == 0) {
		stm32_hcd_adapter_set_poll_mode(false);
	} else {
		shell_print(shell, "usage: usbhost pollmode [on|off]");
		return -EINVAL;
	}

	shell_print(shell, "poll mode: %s",
		    stm32_hcd_adapter_get_poll_mode() ? "on" : "off");
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

static int cmd_usbhost_regs(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/*
	 * Production consistently shows HCINT=0x00000000 with HC_state=2
	 * (halted, presumably by a software URB timeout) on every single
	 * attempt -- never even an error bit set -- unlike rawtest's direct
	 * register polling, which sometimes sees real hardware events
	 * (XFRC/XACTERR). If the global/host interrupt-enable chain
	 * (GINTMSK.HCIM, HAINTMSK, channel-0 HCINTMSK) isn't actually wired
	 * up the same way in the production init path, HAL_HCD_IRQHandler()
	 * would never run for this channel and the driver could only ever
	 * see a timeout, exactly matching what's observed.
	 */
	USB_OTG_GlobalTypeDef *greg = (USB_OTG_GlobalTypeDef *)USB_OTG_FS;
	USB_OTG_HostTypeDef *hreg =
		(USB_OTG_HostTypeDef *)((uint32_t)USB_OTG_FS + USB_OTG_HOST_BASE);

	shell_print(shell, "GINTSTS=0x%08lx GINTMSK=0x%08lx (HCIM bit25=%lu)",
		    (unsigned long)greg->GINTSTS, (unsigned long)greg->GINTMSK,
		    (unsigned long)((greg->GINTMSK >> 25) & 0x1U));
	shell_print(shell, "HAINT=0x%08lx HAINTMSK=0x%08lx (ch0=%lu)",
		    (unsigned long)hreg->HAINT, (unsigned long)hreg->HAINTMSK,
		    (unsigned long)(hreg->HAINTMSK & 0x1U));
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
	SHELL_CMD(rawin, NULL,
		  "Probe configured boot mouse with raw HAL [bulk] (pause first; reset after)",
		  cmd_usbhost_rawin),
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
	SHELL_CMD(regs, NULL,
		  "Dump live GINTMSK/HAINT/HAINTMSK interrupt-enable registers",
		  cmd_usbhost_regs),
	SHELL_CMD(pollmode, NULL,
		  "Toggle bare-metal-style synchronous IRQ polling (usbhost pollmode [on|off])",
		  cmd_usbhost_pollmode),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usbhost, &sub_usbhost, "USB host status commands", NULL);
