/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_boot_mouse.h"
#include "usb_class_select.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "usbh_core.h"
#include "usbh_ctlreq.h"
#include "usbh_pipes.h"

#define HID_CLASS 0x03U
#define HID_BOOT_SUBCLASS 0x01U
#define HID_MOUSE_PROTOCOL 0x02U
#define HID_SET_PROTOCOL 0x0BU
#define MOUSE_REPORT_MAX_SIZE 64U
#define MOUSE_TRANSFER_TIMEOUT_MS 1000U
#define MOUSE_MAX_TRANSFER_ERRORS 10U

struct mouse_state {
	uint8_t pipe;
	uint8_t endpoint;
	uint8_t interface;
	uint8_t interval;
	uint8_t packet_size;
	uint8_t data[MOUSE_REPORT_MAX_SIZE];
	uint32_t next_poll;
	uint32_t transfer_start;
	uint32_t count;
	uint8_t errors;
	struct usb_boot_mouse_report last;
	bool allocated;
	bool pending;
};

static struct mouse_state mouse;
static struct k_spinlock snapshot_lock;
static struct usb_boot_mouse_snapshot mouse_snapshot;

void usb_boot_mouse_snapshot_get(struct usb_boot_mouse_snapshot *snapshot)
{
	k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

	*snapshot = mouse_snapshot;
	k_spin_unlock(&snapshot_lock, key);
}

bool usb_boot_mouse_last_report(struct usb_boot_mouse_report *report,
				uint32_t *count)
{
	if (report == NULL || count == NULL || !mouse.allocated) {
		return false;
	}

	*report = mouse.last;
	*count = mouse.count;
	return true;
}

static USBH_StatusTypeDef mouse_init(USBH_HandleTypeDef *host)
{
	struct usb_function_selection sel;

	memset(&mouse, 0, sizeof(mouse));
	k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

	uint32_t session = mouse_snapshot.session + 1U;

	memset(&mouse_snapshot, 0, sizeof(mouse_snapshot));
	mouse_snapshot.session = session;
	k_spin_unlock(&snapshot_lock, key);
	/* Scan every interface: the boot mouse need not be interface 0. */
	if (usb_function_select(host->device.CfgDesc_Raw,
				sizeof(host->device.CfgDesc_Raw), &sel) != 0 ||
	    sel.kind != USB_FUNCTION_BOOT_MOUSE) {
		printf("ST USBH: unsupported HID interface (boot mouse required)\n");
		return USBH_FAIL;
	}
	if (sel.packet_size > MOUSE_REPORT_MAX_SIZE) {
		printf("ST USBH: boot mouse interrupt-IN endpoint missing or invalid\n");
		return USBH_FAIL;
	}

	mouse.pipe = USBH_AllocPipe(host, sel.endpoint);
	if (mouse.pipe == 0xffU) {
		printf("ST USBH: no free pipe for boot mouse\n");
		return USBH_FAIL;
	}
	mouse.allocated = true;
	mouse.endpoint = sel.endpoint;
	mouse.interface = sel.interface_number;
	mouse.interval = sel.interval;
	mouse.packet_size = (uint8_t)sel.packet_size;
	if (USBH_LL_OpenPipe(host, mouse.pipe, mouse.endpoint, host->device.address,
			     host->device.speed, USBH_EP_INTERRUPT,
			     mouse.packet_size) != USBH_OK) {
		printf("ST USBH: failed to open boot mouse pipe\n");
		USBH_FreePipe(host, mouse.pipe);
		mouse.allocated = false;
		return USBH_FAIL;
	}
	USBH_LL_SetToggle(host, mouse.pipe, 0U);
	return USBH_OK;
}

static USBH_StatusTypeDef mouse_deinit(USBH_HandleTypeDef *host)
{
	k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

	mouse_snapshot.connected = false;
	k_spin_unlock(&snapshot_lock, key);
	if (mouse.allocated) {
		USBH_ClosePipe(host, mouse.pipe);
		USBH_FreePipe(host, mouse.pipe);
	}
	memset(&mouse, 0, sizeof(mouse));
	return USBH_OK;
}

static USBH_StatusTypeDef mouse_requests(USBH_HandleTypeDef *host)
{
	if (host->RequestState == CMD_SEND) {
		host->Control.setup.b.bmRequestType = 0x21U;
		host->Control.setup.b.bRequest = HID_SET_PROTOCOL;
		host->Control.setup.b.wValue.w = 0U;
		host->Control.setup.b.wIndex.w = mouse.interface;
		host->Control.setup.b.wLength.w = 0U;
	}
	USBH_StatusTypeDef result = USBH_CtlReq(host, NULL, 0U);

	if (result == USBH_OK) {
		mouse.next_poll = host->Timer;
		k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

		mouse_snapshot.connected = true;
		k_spin_unlock(&snapshot_lock, key);
		printf("ST USBH: boot mouse ready on EP 0x%02x (interval %u ms)\n",
		       mouse.endpoint, mouse.interval);
	} else if (result != USBH_BUSY) {
		printf("ST USBH: boot mouse SET_PROTOCOL failed: %u\n", result);
		return USBH_FAIL;
	}
	return result;
}

static USBH_StatusTypeDef mouse_process(USBH_HandleTypeDef *host)
{
	if (mouse.pending) {
		USBH_URBStateTypeDef state = USBH_LL_GetURBState(host, mouse.pipe);

		if (state == USBH_URB_DONE) {
			uint32_t length = USBH_LL_GetLastXferSize(host, mouse.pipe);
			struct usb_boot_mouse_report report;

			mouse.pending = false;
			if (length > mouse.packet_size ||
			    !usb_boot_mouse_decode(mouse.data, length, &report)) {
				printf("ST USBH: invalid boot mouse report length %lu\n",
				       (unsigned long)length);
				host->gState = HOST_ABORT_STATE;
				return USBH_FAIL;
			}
			mouse.last = report;
			mouse.count++;
			k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

			usb_boot_mouse_accumulate(&mouse_snapshot, &report);
			k_spin_unlock(&snapshot_lock, key);
			mouse.errors = 0U;
			if (report.buttons != 0U || report.x != 0 ||
			    report.y != 0 || report.wheel != 0) {
				printf("ST USBH: mouse buttons=%u dx=%d dy=%d wheel=%d\n",
				       report.buttons, report.x, report.y, report.wheel);
			}
			mouse.next_poll = host->Timer + mouse.interval;
		} else if (state == USBH_URB_NOTREADY ||
			   state == USBH_URB_NAK_WAIT) {
			mouse.pending = false;
			mouse.next_poll = host->Timer + mouse.interval;
		} else if (state == USBH_URB_ERROR &&
			   ++mouse.errors <= MOUSE_MAX_TRANSFER_ERRORS) {
			printf("ST USBH: mouse interrupt-IN error %u/%u; retrying\n",
			       mouse.errors, MOUSE_MAX_TRANSFER_ERRORS);
			mouse.pending = false;
			mouse.next_poll = host->Timer + mouse.interval;
		} else if (state == USBH_URB_STALL || state == USBH_URB_ERROR ||
			   (host->Timer - mouse.transfer_start) >
			   MOUSE_TRANSFER_TIMEOUT_MS) {
			printf("ST USBH: mouse interrupt-IN failed: URB=%u\n", state);
			host->gState = HOST_ABORT_STATE;
			return USBH_FAIL;
		}
		return USBH_OK;
	}

	if ((int32_t)(host->Timer - mouse.next_poll) < 0) {
		return USBH_OK;
	}
	USBH_StatusTypeDef result = USBH_LL_SubmitURB(host, mouse.pipe, 1U,
						       USBH_EP_INTERRUPT,
						       USBH_PID_DATA, mouse.data,
						       mouse.packet_size, 0U);
	if (result != USBH_OK) {
		printf("ST USBH: mouse interrupt-IN submission failed: %u\n", result);
		host->gState = HOST_ABORT_STATE;
		return USBH_FAIL;
	}
	mouse.transfer_start = host->Timer;
	mouse.pending = true;
	return USBH_OK;
}

static USBH_StatusTypeDef mouse_sof(USBH_HandleTypeDef *host)
{
	(void)host;
	return USBH_OK;
}

USBH_ClassTypeDef usb_boot_mouse_class = {
	.Name = "Boot mouse",
	.ClassCode = HID_CLASS,
	.Init = mouse_init,
	.DeInit = mouse_deinit,
	.Requests = mouse_requests,
	.BgndProcess = mouse_process,
	.SOFProcess = mouse_sof,
};
