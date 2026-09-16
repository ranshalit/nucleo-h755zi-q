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
	int control_read_failures_remaining;
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

	if (hcd->control_read_failures_remaining > 0) {
		hcd->control_read_failures_remaining--;
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
	assert(hcd.control_read_calls == 2);
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
		.control_read_failures_remaining = 3,
	};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_XFER_TIMEOUT);
	assert(hcd.reset_calls == 3);
	assert(hcd.control_read_calls == 3);
	printf("PASS: test_control_transfer_timeout\n");
}

static void test_control_transfer_stall(void)
{
	struct fake_hcd hcd = {
		.control_read_result = -32, /* -EPIPE-like, stall */
		.control_read_failures_remaining = 3,
	};
	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret != 0);
	assert(e.state == USB_ENUM_ERROR);
	assert(e.error == USB_ENUM_ERR_XFER_STALL);
	assert(hcd.reset_calls == 3);
	assert(hcd.control_read_calls == 3);
	printf("PASS: test_control_transfer_stall\n");
}

static void test_control_transfer_recovers_after_retry(void)
{
	struct fake_hcd hcd = {
		.control_read_result = -32,
		.control_read_failures_remaining = 1,
	};
	fill_valid_descriptor(&hcd, 0x1234, 0x5678, 0, 0x0200);

	struct usb_enumerator e;

	usb_enumerator_init(&e, &fake_ops, &hcd);
	usb_enumerator_on_attach(&e);
	int ret = usb_enumerator_run(&e);

	assert(ret == 0);
	assert(e.state == USB_ENUM_DESCRIBED);
	assert(hcd.reset_calls == 2);
	assert(hcd.control_read_calls == 3);
	printf("PASS: test_control_transfer_recovers_after_retry\n");
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
	test_control_transfer_recovers_after_retry();
	test_short_descriptor();
	test_bad_descriptor_type();
	test_overcurrent_from_idle();
	test_detach_during_enumeration();
	test_state_and_error_names_not_null();
	printf("ALL TESTS PASSED\n");
	return 0;
}
