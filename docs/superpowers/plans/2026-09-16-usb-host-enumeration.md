# STM32H7 USB Host Enumeration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `apps/zephyr-hello-shell` configure USB OTG_FS (CN13) as a USB
host, detect the attached HP UVC camera, and print its standard device
descriptor (VID/PID/class) over the existing UART shell — without touching
video/UVC data at all.

**Architecture:** A hardware-independent enumeration state machine
(`usb_enumerator.c`) that only calls an injected `struct usb_hcd_ops`
function-pointer table. A separate STM32-specific adapter
(`stm32_hcd_adapter.c`) implements that table using the vendored STM32H7 HAL
HCD driver (`HAL_HCD_*`) in host mode on `USB_OTG_FS`. A thin shell module
(`usb_host_shell.c`) exposes `usbhost status` / `usbhost descriptor`. The
enumerator is unit-tested on the host with plain `gcc`/`assert` (no Zephyr
build needed) using a fake `usb_hcd_ops` implementation.

**Tech Stack:** Zephyr RTOS (`nucleo_h755zi_q/stm32h755xx/m7`), vendored
STM32H7 Cube HAL (`stm32h7xx_hal_hcd.c/h`, `stm32h7xx_ll_usb.c`), Zephyr
shell subsystem, plain C99 + GCC for host-side unit tests.

## Global Constraints

- Preserve the committed PD10/U18 VBUS enable (`board_early_usb_vbus_init()`
  in `boot_optimizations.c` + the `usb_vbus_enable` GPIO hog in the board
  overlay). Do not move, remove, or duplicate that ownership.
- Do not patch any file under `vendor/zephyrproject/` — application-only
  changes in `apps/zephyr-hello-shell/`.
- Do not enable `CONFIG_USB_HOST_STACK` / `CONFIG_UHC_DRIVER` — no STM32H7
  UHC backend exists in the vendored Zephyr tree.
- Keep LEDs, Ethernet, HTTP server, and existing shell commands
  (`version`, `leds ...`) working exactly as before.
- USB_OTG_FS instance base address is `0x40080000` (node `usbotg_fs`,
  compatible `st,stm32-otgfs`), IRQ name `otgfs` (`OTG_FS_IRQn = 101`).
- Do not implement UVC/video streaming in this plan — descriptor read only.

---

### Task 1: Host-testable enumerator state machine (types + ops interface)

**Files:**
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.h`
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.c`
- Test: `apps/zephyr-hello-shell/tests/usb_host/test_usb_enumerator.c`
- Create: `scripts/run_usb_enumerator_tests.sh`

**Interfaces:**
- Produces: `enum usb_enum_state` (`USB_ENUM_IDLE`, `USB_ENUM_ATTACHED`,
  `USB_ENUM_RESETTING`, `USB_ENUM_ADDRESSING`, `USB_ENUM_DESCRIBED`,
  `USB_ENUM_DETACHED`, `USB_ENUM_ERROR`).
- Produces: `enum usb_enum_error` (`USB_ENUM_ERR_NONE`,
  `USB_ENUM_ERR_ATTACH_TIMEOUT`, `USB_ENUM_ERR_OVERCURRENT`,
  `USB_ENUM_ERR_RESET_FAILED`, `USB_ENUM_ERR_XFER_TIMEOUT`,
  `USB_ENUM_ERR_XFER_STALL`, `USB_ENUM_ERR_XFER_FAILED`,
  `USB_ENUM_ERR_SHORT_DESCRIPTOR`, `USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE`,
  `USB_ENUM_ERR_DETACHED_DURING_ENUM`).
- Produces: `struct usb_device_descriptor_info { uint16_t vendor_id;
  uint16_t product_id; uint8_t device_class; uint16_t usb_version; }`.
- Produces: `struct usb_hcd_ops { int (*reset_port)(void *ctx);
  int (*get_speed)(void *ctx); int (*control_read)(void *ctx,
  uint8_t address, uint8_t bmRequestType, uint8_t bRequest,
  uint16_t wValue, uint16_t wIndex, uint8_t *buf, uint16_t length,
  int32_t timeout_ms); int (*set_address)(void *ctx, uint8_t address,
  int32_t timeout_ms); }`.
- Produces: `struct usb_enumerator { const struct usb_hcd_ops *ops;
  void *ctx; enum usb_enum_state state; enum usb_enum_error error;
  struct usb_device_descriptor_info descriptor; }`.
- Produces: `void usb_enumerator_init(struct usb_enumerator *e,
  const struct usb_hcd_ops *ops, void *ctx);`
- Produces: `void usb_enumerator_on_attach(struct usb_enumerator *e);`
- Produces: `void usb_enumerator_on_detach(struct usb_enumerator *e);`
- Produces: `void usb_enumerator_on_overcurrent(struct usb_enumerator *e);`
- Produces: `int usb_enumerator_run(struct usb_enumerator *e);` — drives
  reset → address → descriptor read; returns 0 on reaching
  `USB_ENUM_DESCRIBED` or a negative value while `state == USB_ENUM_ERROR`.
- Produces: `const char *usb_enum_state_name(enum usb_enum_state s);`
- Produces: `const char *usb_enum_error_name(enum usb_enum_error e);`

- [ ] **Step 1: Write the failing test file**

Create `apps/zephyr-hello-shell/tests/usb_host/test_usb_enumerator.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "usb_enumerator.h"

/* --- Fake HCD ops used to drive the enumerator without hardware --- */

struct fake_hcd {
	int reset_result;
	int speed_result;
	int control_read_result;
	int set_address_result;
	uint8_t device_descriptor[18];
	int control_read_calls;
	int set_address_calls;
	int reset_calls;
};

static int fake_reset_port(void *ctx)
{
	struct fake_hcd *hcd = ctx;

	hcd->reset_calls++;
	return hcd->reset_result;
}

static int fake_get_speed(void *ctx)
{
	struct fake_hcd *hcd = ctx;

	return hcd->speed_result;
}

static int fake_control_read(void *ctx, uint8_t address, uint8_t bmRequestType,
			      uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
			      uint8_t *buf, uint16_t length, int32_t timeout_ms)
{
	struct fake_hcd *hcd = ctx;

	(void)address;
	(void)bmRequestType;
	(void)bRequest;
	(void)wValue;
	(void)wIndex;
	(void)timeout_ms;

	hcd->control_read_calls++;

	if (hcd->control_read_result != 0) {
		return hcd->control_read_result;
	}

	uint16_t copy_len = length < sizeof(hcd->device_descriptor) ?
			    length : sizeof(hcd->device_descriptor);

	memcpy(buf, hcd->device_descriptor, copy_len);
	return copy_len;
}

static int fake_set_address(void *ctx, uint8_t address, int32_t timeout_ms)
{
	struct fake_hcd *hcd = ctx;

	(void)address;
	(void)timeout_ms;

	hcd->set_address_calls++;
	return hcd->set_address_result;
}

static const struct usb_hcd_ops fake_ops = {
	.reset_port = fake_reset_port,
	.get_speed = fake_get_speed,
	.control_read = fake_control_read,
	.set_address = fake_set_address,
};

static void fill_valid_descriptor(struct fake_hcd *hcd, uint16_t vid,
				   uint16_t pid, uint8_t device_class,
				   uint16_t usb_version)
{
	memset(hcd->device_descriptor, 0, sizeof(hcd->device_descriptor));
	hcd->device_descriptor[0] = 18;   /* bLength */
	hcd->device_descriptor[1] = 0x01; /* bDescriptorType = DEVICE */
	hcd->device_descriptor[2] = (uint8_t)(usb_version & 0xFF);
	hcd->device_descriptor[3] = (uint8_t)(usb_version >> 8);
	hcd->device_descriptor[4] = device_class;
	hcd->device_descriptor[8] = (uint8_t)(vid & 0xFF);
	hcd->device_descriptor[9] = (uint8_t)(vid >> 8);
	hcd->device_descriptor[10] = (uint8_t)(pid & 0xFF);
	hcd->device_descriptor[11] = (uint8_t)(pid >> 8);
}

static void test_successful_enumeration(void)
{
	struct fake_hcd hcd = {
		.reset_result = 0,
		.speed_result = 1,
		.control_read_result = 0,
		.set_address_result = 0,
	};
	fill_valid_descriptor(&hcd, 0x05c8, 0x0b06, 0x0E /* video */, 0x0200);

	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret == 0);
	assert(e.state == USB_ENUM_DESCRIBED);
	assert(e.error == USB_ENUM_ERR_NONE);
	assert(e.descriptor.vendor_id == 0x05c8);
	assert(e.descriptor.product_id == 0x0b06);
	assert(e.descriptor.device_class == 0x0E);
	assert(e.descriptor.usb_version == 0x0200);
	assert(hcd.reset_calls == 1);
	assert(hcd.set_address_calls == 1);
	printf("PASS: test_successful_enumeration\n");
}

static void test_idle_before_attach(void)
{
	struct fake_hcd hcd = {0};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	assert(e.state == USB_ENUM_IDLE);
	printf("PASS: test_idle_before_attach\n");
}

static void test_reset_failure(void)
{
	struct fake_hcd hcd = {
		.reset_result = -1,
	};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_RESET_FAILED);
	printf("PASS: test_reset_failure\n");
}

static void test_control_transfer_timeout(void)
{
	struct fake_hcd hcd = {
		.control_read_result = -110, /* -ETIMEDOUT-like */
	};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_XFER_TIMEOUT);
	printf("PASS: test_control_transfer_timeout\n");
}

static void test_control_transfer_stall(void)
{
	struct fake_hcd hcd = {
		.control_read_result = -32, /* -EPIPE-like, stall */
	};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_XFER_STALL);
	printf("PASS: test_control_transfer_stall\n");
}

static void test_short_descriptor(void)
{
	struct fake_hcd hcd = {0};

	fill_valid_descriptor(&hcd, 0x05c8, 0x0b06, 0x0E, 0x0200);
	hcd.device_descriptor[0] = 8; /* bLength too small */

	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_SHORT_DESCRIPTOR);
	printf("PASS: test_short_descriptor\n");
}

static void test_bad_descriptor_type(void)
{
	struct fake_hcd hcd = {0};

	fill_valid_descriptor(&hcd, 0x05c8, 0x0b06, 0x0E, 0x0200);
	hcd.device_descriptor[1] = 0x02; /* CONFIGURATION, not DEVICE */

	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE);
	printf("PASS: test_bad_descriptor_type\n");
}

static void test_overcurrent_from_idle(void)
{
	struct fake_hcd hcd = {0};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_overcurrent(&e);

	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_OVERCURRENT);
	printf("PASS: test_overcurrent_from_idle\n");
}

static void test_detach_during_enumeration(void)
{
	struct fake_hcd hcd = {0};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	usb_enumerator_on_detach(&e);

	assert(e.state == USB_ENUM_DETACHED);
	assert(e.error == USB_ENUM_ERR_DETACHED_DURING_ENUM);
	printf("PASS: test_detach_during_enumeration\n");
}

static void test_state_and_error_names_not_null(void)
{
	assert(usb_enum_state_name(USB_ENUM_DESCRIBED) != NULL);
	assert(usb_enum_error_name(USB_ENUM_ERR_OVERCURRENT) != NULL);
	printf("PASS: test_state_and_error_names_not_null\n");
}

int main(void)
{
	test_idle_before_attach();
	test_successful_enumeration();
	test_reset_failure();
	test_control_transfer_timeout();
	test_control_transfer_stall();
	test_short_descriptor();
	test_bad_descriptor_type();
	test_overcurrent_from_idle();
	test_detach_during_enumeration();
	test_state_and_error_names_not_null();
	printf("ALL TESTS PASSED\n");
	return 0;
}
```

- [ ] **Step 2: Create the test runner script**

Create `scripts/run_usb_enumerator_tests.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/src/usb_host"
TEST_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/tests/usb_host"
BUILD_DIR="${ZEPHYR_HOST_TEST_BUILD_DIR:-/tmp/usb-enumerator-test}"

mkdir -p "${BUILD_DIR}"

gcc -std=c99 -Wall -Wextra -Werror \
  -I"${SRC_DIR}" \
  "${TEST_DIR}/test_usb_enumerator.c" \
  "${SRC_DIR}/usb_enumerator.c" \
  -o "${BUILD_DIR}/test_usb_enumerator"

"${BUILD_DIR}/test_usb_enumerator"
```

Make it executable:

```bash
chmod +x scripts/run_usb_enumerator_tests.sh
```

- [ ] **Step 3: Create the header (interface only, no logic yet)**

Create `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_ENUMERATOR_H_
#define USB_ENUMERATOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum usb_enum_state {
	USB_ENUM_IDLE = 0,
	USB_ENUM_ATTACHED,
	USB_ENUM_RESETTING,
	USB_ENUM_ADDRESSING,
	USB_ENUM_DESCRIBED,
	USB_ENUM_DETACHED,
	USB_ENUM_ERROR,
};

enum usb_enum_error {
	USB_ENUM_ERR_NONE = 0,
	USB_ENUM_ERR_ATTACH_TIMEOUT,
	USB_ENUM_ERR_OVERCURRENT,
	USB_ENUM_ERR_RESET_FAILED,
	USB_ENUM_ERR_XFER_TIMEOUT,
	USB_ENUM_ERR_XFER_STALL,
	USB_ENUM_ERR_XFER_FAILED,
	USB_ENUM_ERR_SHORT_DESCRIPTOR,
	USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE,
	USB_ENUM_ERR_DETACHED_DURING_ENUM,
};

struct usb_device_descriptor_info {
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t device_class;
	uint16_t usb_version;
};

struct usb_hcd_ops {
	int (*reset_port)(void *ctx);
	int (*get_speed)(void *ctx);
	int (*control_read)(void *ctx, uint8_t address, uint8_t bmRequestType,
			     uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
			     uint8_t *buf, uint16_t length, int32_t timeout_ms);
	int (*set_address)(void *ctx, uint8_t address, int32_t timeout_ms);
};

struct usb_enumerator {
	const struct usb_hcd_ops *ops;
	void *ctx;
	enum usb_enum_state state;
	enum usb_enum_error error;
	struct usb_device_descriptor_info descriptor;
};

void usb_enumerator_init(struct usb_enumerator *e, const struct usb_hcd_ops *ops, void *ctx);
void usb_enumerator_on_attach(struct usb_enumerator *e);
void usb_enumerator_on_detach(struct usb_enumerator *e);
void usb_enumerator_on_overcurrent(struct usb_enumerator *e);
int usb_enumerator_run(struct usb_enumerator *e);
const char *usb_enum_state_name(enum usb_enum_state s);
const char *usb_enum_error_name(enum usb_enum_error e);

#ifdef __cplusplus
}
#endif

#endif /* USB_ENUMERATOR_H_ */
```

- [ ] **Step 4: Create a stub implementation so the test compiles and fails on assertions**

Create `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_enumerator.h"

#include <stddef.h>

void usb_enumerator_init(struct usb_enumerator *e, const struct usb_hcd_ops *ops, void *ctx)
{
	e->ops = ops;
	e->ctx = ctx;
	e->state = USB_ENUM_IDLE;
	e->error = USB_ENUM_ERR_NONE;
	e->descriptor.vendor_id = 0;
	e->descriptor.product_id = 0;
	e->descriptor.device_class = 0;
	e->descriptor.usb_version = 0;
}

void usb_enumerator_on_attach(struct usb_enumerator *e)
{
	(void)e;
}

void usb_enumerator_on_detach(struct usb_enumerator *e)
{
	(void)e;
}

void usb_enumerator_on_overcurrent(struct usb_enumerator *e)
{
	(void)e;
}

int usb_enumerator_run(struct usb_enumerator *e)
{
	(void)e;
	return -1;
}

const char *usb_enum_state_name(enum usb_enum_state s)
{
	(void)s;
	return "unknown";
}

const char *usb_enum_error_name(enum usb_enum_error e)
{
	(void)e;
	return "unknown";
}
```

- [ ] **Step 5: Run the test to verify it fails**

Run: `scripts/run_usb_enumerator_tests.sh`
Expected: builds successfully, then aborts on the first `assert` in
`test_idle_before_attach` or `test_successful_enumeration` (stub always
returns idle/-1). Confirm it fails for the right reason: `Assertion
'e.state == USB_ENUM_DESCRIBED' failed` (or similar), not a compile error.

- [ ] **Step 6: Implement the real enumerator logic**

Replace `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.c` with:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_enumerator.h"

#include <stddef.h>
#include <string.h>

#define USB_ENUM_CONTROL_TIMEOUT_MS 500
#define USB_ENUM_SET_ADDRESS_TIMEOUT_MS 50
#define USB_ENUM_DEVICE_ADDRESS 1
#define USB_DEVICE_DESCRIPTOR_LENGTH 18
#define USB_DESCRIPTOR_TYPE_DEVICE 0x01
#define USB_DEVICE_TO_HOST_STANDARD_DEVICE 0x80
#define USB_REQUEST_GET_DESCRIPTOR 0x06

static void set_error(struct usb_enumerator *e, enum usb_enum_error error)
{
	e->state = USB_ENUM_ERROR;
	e->error = error;
}

static enum usb_enum_error classify_xfer_error(int ret)
{
	if (ret == -110 || ret == -116) {
		return USB_ENUM_ERR_XFER_TIMEOUT;
	}
	if (ret == -32 || ret == -71) {
		return USB_ENUM_ERR_XFER_STALL;
	}
	return USB_ENUM_ERR_XFER_FAILED;
}

void usb_enumerator_init(struct usb_enumerator *e, const struct usb_hcd_ops *ops, void *ctx)
{
	e->ops = ops;
	e->ctx = ctx;
	e->state = USB_ENUM_IDLE;
	e->error = USB_ENUM_ERR_NONE;
	memset(&e->descriptor, 0, sizeof(e->descriptor));
}

void usb_enumerator_on_attach(struct usb_enumerator *e)
{
	if (e->state == USB_ENUM_IDLE || e->state == USB_ENUM_DETACHED ||
	    e->state == USB_ENUM_ERROR) {
		e->state = USB_ENUM_ATTACHED;
		e->error = USB_ENUM_ERR_NONE;
	}
}

void usb_enumerator_on_detach(struct usb_enumerator *e)
{
	if (e->state == USB_ENUM_ATTACHED || e->state == USB_ENUM_RESETTING ||
	    e->state == USB_ENUM_ADDRESSING) {
		e->error = USB_ENUM_ERR_DETACHED_DURING_ENUM;
	} else {
		e->error = USB_ENUM_ERR_NONE;
	}
	e->state = USB_ENUM_DETACHED;
}

void usb_enumerator_on_overcurrent(struct usb_enumerator *e)
{
	set_error(e, USB_ENUM_ERR_OVERCURRENT);
}

static int parse_device_descriptor(struct usb_enumerator *e, const uint8_t *buf, int len)
{
	if (len < USB_DEVICE_DESCRIPTOR_LENGTH || buf[0] < USB_DEVICE_DESCRIPTOR_LENGTH) {
		set_error(e, USB_ENUM_ERR_SHORT_DESCRIPTOR);
		return -1;
	}
	if (buf[1] != USB_DESCRIPTOR_TYPE_DEVICE) {
		set_error(e, USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE);
		return -1;
	}

	e->descriptor.usb_version = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));
	e->descriptor.device_class = buf[4];
	e->descriptor.vendor_id = (uint16_t)(buf[8] | ((uint16_t)buf[9] << 8));
	e->descriptor.product_id = (uint16_t)(buf[10] | ((uint16_t)buf[11] << 8));
	return 0;
}

int usb_enumerator_run(struct usb_enumerator *e)
{
	uint8_t descriptor_buf[USB_DEVICE_DESCRIPTOR_LENGTH];
	int ret;

	if (e->state != USB_ENUM_ATTACHED) {
		return e->state == USB_ENUM_DESCRIBED ? 0 : -1;
	}

	e->state = USB_ENUM_RESETTING;
	ret = e->ops->reset_port(e->ctx);
	if (ret != 0) {
		set_error(e, USB_ENUM_ERR_RESET_FAILED);
		return -1;
	}

	(void)e->ops->get_speed(e->ctx);

	e->state = USB_ENUM_ADDRESSING;
	ret = e->ops->control_read(e->ctx, 0, USB_DEVICE_TO_HOST_STANDARD_DEVICE,
				    USB_REQUEST_GET_DESCRIPTOR,
				    (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8), 0,
				    descriptor_buf, sizeof(descriptor_buf),
				    USB_ENUM_CONTROL_TIMEOUT_MS);
	if (ret < 0) {
		set_error(e, classify_xfer_error(ret));
		return -1;
	}
	if (parse_device_descriptor(e, descriptor_buf, ret) != 0) {
		return -1;
	}

	ret = e->ops->set_address(e->ctx, USB_ENUM_DEVICE_ADDRESS,
				   USB_ENUM_SET_ADDRESS_TIMEOUT_MS);
	if (ret != 0) {
		set_error(e, USB_ENUM_ERR_XFER_FAILED);
		return -1;
	}

	ret = e->ops->control_read(e->ctx, USB_ENUM_DEVICE_ADDRESS,
				    USB_DEVICE_TO_HOST_STANDARD_DEVICE,
				    USB_REQUEST_GET_DESCRIPTOR,
				    (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8), 0,
				    descriptor_buf, sizeof(descriptor_buf),
				    USB_ENUM_CONTROL_TIMEOUT_MS);
	if (ret < 0) {
		set_error(e, classify_xfer_error(ret));
		return -1;
	}
	if (parse_device_descriptor(e, descriptor_buf, ret) != 0) {
		return -1;
	}

	e->state = USB_ENUM_DESCRIBED;
	e->error = USB_ENUM_ERR_NONE;
	return 0;
}

const char *usb_enum_state_name(enum usb_enum_state s)
{
	switch (s) {
	case USB_ENUM_IDLE:       return "idle";
	case USB_ENUM_ATTACHED:   return "attached";
	case USB_ENUM_RESETTING:  return "resetting";
	case USB_ENUM_ADDRESSING: return "addressing";
	case USB_ENUM_DESCRIBED:  return "described";
	case USB_ENUM_DETACHED:   return "detached";
	case USB_ENUM_ERROR:      return "error";
	default:                  return "unknown";
	}
}

const char *usb_enum_error_name(enum usb_enum_error e)
{
	switch (e) {
	case USB_ENUM_ERR_NONE:                  return "none";
	case USB_ENUM_ERR_ATTACH_TIMEOUT:        return "attach_timeout";
	case USB_ENUM_ERR_OVERCURRENT:           return "overcurrent";
	case USB_ENUM_ERR_RESET_FAILED:          return "reset_failed";
	case USB_ENUM_ERR_XFER_TIMEOUT:          return "xfer_timeout";
	case USB_ENUM_ERR_XFER_STALL:            return "xfer_stall";
	case USB_ENUM_ERR_XFER_FAILED:           return "xfer_failed";
	case USB_ENUM_ERR_SHORT_DESCRIPTOR:      return "short_descriptor";
	case USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE:   return "bad_descriptor_type";
	case USB_ENUM_ERR_DETACHED_DURING_ENUM:  return "detached_during_enum";
	default:                                 return "unknown";
	}
}
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `scripts/run_usb_enumerator_tests.sh`
Expected: `ALL TESTS PASSED` printed, exit code 0.

- [ ] **Step 8: Commit**

```bash
git add apps/zephyr-hello-shell/src/usb_host/usb_enumerator.h \
        apps/zephyr-hello-shell/src/usb_host/usb_enumerator.c \
        apps/zephyr-hello-shell/tests/usb_host/test_usb_enumerator.c \
        scripts/run_usb_enumerator_tests.sh
git commit -m "feat(usb): add host-independent enumeration state machine"
```

---

### Task 2: STM32 HCD adapter (Zephyr + HAL integration)

**Files:**
- Create: `apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.h`
- Create: `apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.c`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Modify: `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`

**Interfaces:**
- Consumes: `struct usb_hcd_ops`, `struct usb_enumerator`,
  `usb_enumerator_init`, `usb_enumerator_on_attach`,
  `usb_enumerator_on_detach`, `usb_enumerator_on_overcurrent`,
  `usb_enumerator_run` from Task 1 (`usb_enumerator.h`).
- Produces: `int stm32_hcd_adapter_init(void);` — configures OTG_FS in host
  mode, installs the IRQ handler, returns 0 on success or a negative errno.
- Produces: `struct usb_enumerator *stm32_hcd_adapter_enumerator(void);` —
  returns a pointer to the single static enumerator instance so the shell
  module (Task 3) can read its state/descriptor.
- Produces: `void stm32_hcd_adapter_poll(void);` — must be called
  periodically (from a Zephyr thread) to detect attach/detach edges via
  `HAL_HCD_GetCurrentSpeed` disconnect polling isn't reliable, so this
  function drains a lock-free event flag set by the HAL callbacks
  (`HAL_HCD_Connect_Callback`, `HAL_HCD_Disconnect_Callback`) and drives
  `usb_enumerator_run()`.

- [ ] **Step 1: Create the adapter header**

Create `apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef STM32_HCD_ADAPTER_H_
#define STM32_HCD_ADAPTER_H_

#include "usb_enumerator.h"

#ifdef __cplusplus
extern "C" {
#endif

int stm32_hcd_adapter_init(void);
struct usb_enumerator *stm32_hcd_adapter_enumerator(void);
void stm32_hcd_adapter_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_HCD_ADAPTER_H_ */
```

- [ ] **Step 2: Create the adapter implementation**

Create `apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_hcd.h>
#include <stm32h7xx_ll_bus.h>
#include <stm32h7xx_ll_gpio.h>

LOG_MODULE_REGISTER(stm32_hcd_adapter, LOG_LEVEL_INF);

#define STM32_HCD_IRQ 101 /* OTG_FS_IRQn, RM0468 Table 135 */
#define STM32_HCD_IRQ_PRI 5
#define STM32_HCD_CHANNEL_CONTROL 0
#define STM32_HCD_CONTROL_MPS 64U

/* Event bits set from HAL callbacks (ISR context), drained by stm32_hcd_adapter_poll(). */
#define EVT_CONNECT    BIT(0)
#define EVT_DISCONNECT BIT(1)
#define EVT_OVERCURRENT BIT(2)

static HCD_HandleTypeDef hcd_handle;
static struct usb_enumerator enumerator;
static atomic_t pending_events;

static int adapter_reset_port(void *ctx)
{
	HCD_HandleTypeDef *hcd = ctx;

	if (HAL_HCD_ResetPort(hcd) != HAL_OK) {
		return -1;
	}
	k_msleep(50); /* USB reset recovery time, USB 2.0 spec 7.1.7.5 */
	return 0;
}

static int adapter_get_speed(void *ctx)
{
	HCD_HandleTypeDef *hcd = ctx;

	return (int)HAL_HCD_GetCurrentSpeed(hcd);
}

static int adapter_control_read(void *ctx, uint8_t address, uint8_t bmRequestType,
				 uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
				 uint8_t *buf, uint16_t length, int32_t timeout_ms)
{
	HCD_HandleTypeDef *hcd = ctx;
	uint8_t setup_pkt[8];
	int64_t deadline;

	setup_pkt[0] = bmRequestType;
	setup_pkt[1] = bRequest;
	setup_pkt[2] = (uint8_t)(wValue & 0xFF);
	setup_pkt[3] = (uint8_t)(wValue >> 8);
	setup_pkt[4] = (uint8_t)(wIndex & 0xFF);
	setup_pkt[5] = (uint8_t)(wIndex >> 8);
	setup_pkt[6] = (uint8_t)(length & 0xFF);
	setup_pkt[7] = (uint8_t)(length >> 8);

	(void)HAL_HCD_HC_Init(hcd, STM32_HCD_CHANNEL_CONTROL, 0, address,
			      HCD_DEVICE_SPEED_FULL, EP_TYPE_CTRL, STM32_HCD_CONTROL_MPS);

	if (HAL_HCD_HC_SubmitRequest(hcd, STM32_HCD_CHANNEL_CONTROL, 0, EP_TYPE_CTRL,
				     0 /* SETUP */, setup_pkt, sizeof(setup_pkt), 0) != HAL_OK) {
		return -71; /* -EPROTO-like */
	}

	deadline = k_uptime_get() + timeout_ms;
	while (HAL_HCD_HC_GetState(hcd, STM32_HCD_CHANNEL_CONTROL) != HC_XFRC) {
		if (k_uptime_get() > deadline) {
			return -110; /* -ETIMEDOUT */
		}
		k_msleep(1);
	}

	if (HAL_HCD_HC_SubmitRequest(hcd, STM32_HCD_CHANNEL_CONTROL, 1, EP_TYPE_CTRL,
				     1 /* DATA1 */, buf, length, 0) != HAL_OK) {
		return -71;
	}

	deadline = k_uptime_get() + timeout_ms;
	while (HAL_HCD_HC_GetState(hcd, STM32_HCD_CHANNEL_CONTROL) != HC_XFRC) {
		if (k_uptime_get() > deadline) {
			return -110;
		}
		if (HAL_HCD_HC_GetState(hcd, STM32_HCD_CHANNEL_CONTROL) == HC_STALL) {
			return -32; /* -EPIPE */
		}
		k_msleep(1);
	}

	return (int)HAL_HCD_HC_GetXferCount(hcd, STM32_HCD_CHANNEL_CONTROL);
}

static int adapter_set_address(void *ctx, uint8_t address, int32_t timeout_ms)
{
	HCD_HandleTypeDef *hcd = ctx;
	uint8_t setup_pkt[8] = {
		0x00, 0x05, address, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	int64_t deadline;

	if (HAL_HCD_HC_SubmitRequest(hcd, STM32_HCD_CHANNEL_CONTROL, 0, EP_TYPE_CTRL,
				     0, setup_pkt, sizeof(setup_pkt), 0) != HAL_OK) {
		return -1;
	}

	deadline = k_uptime_get() + timeout_ms;
	while (HAL_HCD_HC_GetState(hcd, STM32_HCD_CHANNEL_CONTROL) != HC_XFRC) {
		if (k_uptime_get() > deadline) {
			return -1;
		}
		k_msleep(1);
	}
	return 0;
}

static const struct usb_hcd_ops stm32_ops = {
	.reset_port = adapter_reset_port,
	.get_speed = adapter_get_speed,
	.control_read = adapter_control_read,
	.set_address = adapter_set_address,
};

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
	ARG_UNUSED(hhcd);
	atomic_or(&pending_events, EVT_CONNECT);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
	ARG_UNUSED(hhcd);
	atomic_or(&pending_events, EVT_DISCONNECT);
}

static void stm32_hcd_isr(const void *arg)
{
	ARG_UNUSED(arg);
	HAL_HCD_IRQHandler(&hcd_handle);
}

int stm32_hcd_adapter_init(void)
{
	usb_enumerator_init(&enumerator, &stm32_ops, &hcd_handle);

	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_USB2OTGHS);

	memset(&hcd_handle, 0, sizeof(hcd_handle));
	hcd_handle.Instance = USB_OTG_FS;
	hcd_handle.Init.Host_channels = 8;
	hcd_handle.Init.speed = HCD_SPEED_FULL;
	hcd_handle.Init.dma_enable = DISABLE;
	hcd_handle.Init.phy_itface = HCD_PHY_EMBEDDED;
	hcd_handle.Init.Sof_enable = DISABLE;
	hcd_handle.Init.low_power_enable = DISABLE;
	hcd_handle.Init.vbus_sensing_enable = DISABLE;
	hcd_handle.Init.use_external_vbus = DISABLE;

	if (HAL_HCD_Init(&hcd_handle) != HAL_OK) {
		LOG_ERR("HAL_HCD_Init failed");
		return -EIO;
	}

	IRQ_CONNECT(STM32_HCD_IRQ, STM32_HCD_IRQ_PRI, stm32_hcd_isr, NULL, 0);
	irq_enable(STM32_HCD_IRQ);

	if (HAL_HCD_Start(&hcd_handle) != HAL_OK) {
		LOG_ERR("HAL_HCD_Start failed");
		return -EIO;
	}

	LOG_INF("usbhost: ready");
	return 0;
}

struct usb_enumerator *stm32_hcd_adapter_enumerator(void)
{
	return &enumerator;
}

void stm32_hcd_adapter_poll(void)
{
	atomic_val_t events = atomic_clear(&pending_events);

	if (events & EVT_DISCONNECT) {
		usb_enumerator_on_detach(&enumerator);
		LOG_INF("usbhost: detached");
	}
	if (events & EVT_CONNECT) {
		usb_enumerator_on_attach(&enumerator);
		LOG_INF("usbhost: attached, enumerating");
		if (usb_enumerator_run(&enumerator) == 0) {
			LOG_INF("usbhost: described vid=0x%04x pid=0x%04x class=0x%02x",
				enumerator.descriptor.vendor_id,
				enumerator.descriptor.product_id,
				enumerator.descriptor.device_class);
		} else {
			LOG_WRN("usbhost: enumeration failed (%s)",
				usb_enum_error_name(enumerator.error));
		}
	}
}
```

- [ ] **Step 3: Register the new sources in CMakeLists.txt**

In `apps/zephyr-hello-shell/CMakeLists.txt`, modify the `target_sources`
block:

```cmake
target_sources(app PRIVATE
  src/boot_optimizations.c
  src/main.c
  src/board_io.c
  src/http_server.c
  src/web_assets.c
  src/usb_host/usb_enumerator.c
  src/usb_host/stm32_hcd_adapter.c
  src/usb_host/usb_host_shell.c
)

target_include_directories(app PRIVATE
  src/usb_host
)
```

(`usb_host_shell.c` is created in Task 3; add it here now so Task 3 doesn't
need to touch CMakeLists.txt again.)

- [ ] **Step 4: Enable HAL HCD module in the board-specific config**

`CONFIG_USE_STM32_HAL_HCD` is confirmed to exist and is wired into the H7
build (`vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/CMakeLists.txt:41`:
`zephyr_library_sources_ifdef(CONFIG_USE_STM32_HAL_HCD drivers/src/stm32h7xx_hal_hcd.c)`).
Enabling it is what pulls `stm32h7xx_hal_hcd.c` into the build; without it,
`HAL_HCD_Init` and friends will fail to link.

Modify `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`,
append:

```
CONFIG_USE_STM32_HAL_HCD=y
```

- [ ] **Step 5: Build the application**

Run: `scripts/flash_zephyr_hello_shell.sh --build-only`
Expected: build succeeds with no errors. If it fails on an unresolved HAL
symbol (e.g. `HAL_HCD_Init` undefined), check
`vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c`
is compiled in — it's pulled in automatically because
`HAL_HCD_MODULE_ENABLED` is defined in `stm32h7xx_hal_conf.h` and Zephyr's
STM32 HAL CMake glob-includes all `stm32h7xx_hal_*.c` files matching enabled
modules; if not, report the exact linker error rather than guessing further.

- [ ] **Step 6: Commit**

```bash
git add apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.h \
        apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.c \
        apps/zephyr-hello-shell/CMakeLists.txt \
        apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf
git commit -m "feat(usb): add STM32 HCD host-mode adapter"
```

---

### Task 3: Shell integration and polling thread

**Files:**
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c`
- Modify: `apps/zephyr-hello-shell/src/main.c`

**Interfaces:**
- Consumes: `stm32_hcd_adapter_init`, `stm32_hcd_adapter_enumerator`,
  `stm32_hcd_adapter_poll` from Task 2 (`stm32_hcd_adapter.h`).
- Consumes: `usb_enum_state_name`, `usb_enum_error_name` from Task 1
  (`usb_enumerator.h`).
- Produces: `int board_usb_host_start(void);` — called once from `main()`,
  starts the polling thread; returns the adapter init result.

- [ ] **Step 1: Create the shell module with a background polling thread**

Create `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

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
```

- [ ] **Step 2: Call `board_usb_host_start()` from `main()`**

In `apps/zephyr-hello-shell/src/main.c`, add the include near the top:

```c
#include "usb_host/stm32_hcd_adapter.h"
```

Then, after the existing `board_http_server_start()` block (right before
the final `return 0;`), add:

```c
	ret = board_usb_host_start();
	if (ret != 0) {
		printf("USB host startup failed: %d\n", ret);
		/* Non-fatal: continue running LEDs/HTTP even if USB host init failed. */
	}
```

Note: `board_usb_host_start` is declared via
`usb_host/usb_host_shell.c` — since it has no matching header, add its
prototype directly above `int main(void)` in `main.c`:

```c
extern int board_usb_host_start(void);
```

- [ ] **Step 3: Build the application**

Run: `scripts/flash_zephyr_hello_shell.sh --build-only`
Expected: build succeeds with no errors or warnings treated as errors.

- [ ] **Step 4: Commit**

```bash
git add apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c \
        apps/zephyr-hello-shell/src/main.c
git commit -m "feat(usb): expose usbhost shell commands and start polling"
```

---

### Task 4: Hardware validation with the attached camera

**Files:**
- None created/modified — this task only flashes and observes.

**Interfaces:**
- Consumes: `usbhost status` / `usbhost descriptor` shell commands from
  Task 3.

- [ ] **Step 1: Confirm solder bridges and VBUS power**

Visually confirm SB76 and SB77 are ON (per UM2407 Table — already assumed
true since the board is in daily use for CN13). Note in the task output if
they cannot be confirmed without opening the board; if LD8 doesn't light
after flashing, this is the first thing to check.

- [ ] **Step 2: Flash the firmware**

Run: `scripts/flash_zephyr_hello_shell.sh --flash-only --skip-validate`
Expected: "Application is running" printed by STM32_Programmer_CLI.

- [ ] **Step 3: Open the serial console and check boot log**

Run (or use the serial-console skill):
```bash
stty -F /dev/ttyACM0 115200 raw -echo -echoe -echok
```
Then read a few seconds of output. Expected to see `usbhost: ready` among
the boot log lines (from `LOG_INF` in `stm32_hcd_adapter_init`).

- [ ] **Step 4: Insert the HP camera and query status**

With the camera already connected to CN13 (per the user's setup), run at the
`uart:~$` prompt:
```
usbhost status
```
Expected one of:
- `state: described` / `error: none` — enumeration succeeded, proceed to
  Step 5.
- `state: error` / `error: <reason>` — record the exact reason string; this
  is the actual hardware result and must be reported to the user verbatim,
  not assumed away.

- [ ] **Step 5: Read the descriptor if described**

Run:
```
usbhost descriptor
```
Expected output showing `vendor_id: 0x05c8`, `product_id: 0x0b06` (matching
the `lsusb` result already captured for the "Cheng Uei Precision Industry
Co., Ltd (Foxlink) HP HD Camera": `ID 05c8:0b06`), and
`device_class: 0x0e` (video, since it's a UVC camera reporting class at the
interface level would actually show `0x00` — record whichever value the
board reports, don't assume 0x0e is correct until confirmed by hardware).

- [ ] **Step 6: Confirm existing behavior is unaffected**

Run:
```
version
leds on all
leds off all
```
Confirm LED responses still work, and separately confirm (via `ip addr` on
host or the existing validated `net ping`) that Ethernet still functions
per the existing `scripts/flash_zephyr_hello_shell.sh` validation flow:

Run: `scripts/flash_zephyr_hello_shell.sh --validate-only`
Expected: existing UART/LED/Ethernet/HTTP validations pass unchanged.

- [ ] **Step 7: Record the result**

No commit for this task — report the exact `usbhost status` /
`usbhost descriptor` output (or exact error) to the user. If enumeration
failed, do not modify the enumerator/adapter blindly; capture the precise
error and treat further fixes as a new investigation (systematic-debugging
skill), since bit-level USB host timing issues need root-cause evidence, not
guesses.

---

## Post-Plan Notes

This plan intentionally stops at descriptor-level enumeration. UVC
configuration-descriptor parsing, isochronous streaming, and frame delivery
are out of scope and require a separate spec/plan once enumeration is
confirmed working on hardware.
