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
	unsigned int attempt;
	int ret;

	if (e->state != USB_ENUM_ATTACHED) {
		return e->state == USB_ENUM_DESCRIBED ? 0 : -1;
	}

	for (attempt = 0; attempt < 3; attempt++) {
		e->state = USB_ENUM_RESETTING;
		ret = e->ops->reset_port(e->ctx);
		if (ret != 0) {
			set_error(e, USB_ENUM_ERR_RESET_FAILED);
			return -1;
		}

		(void)e->ops->get_speed(e->ctx);

		e->state = USB_ENUM_ADDRESSING;
		ret = e->ops->control_read(e->ctx, 0,
					    USB_DEVICE_TO_HOST_STANDARD_DEVICE,
					    USB_REQUEST_GET_DESCRIPTOR,
					    (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8), 0,
					    descriptor_buf, 8,
					    USB_ENUM_CONTROL_TIMEOUT_MS);
		if (ret >= 0) {
			break;
		}
	}
	if (ret < 0) {
		set_error(e, classify_xfer_error(ret));
		return -1;
	}
	if (ret < 8 || descriptor_buf[0] < USB_DEVICE_DESCRIPTOR_LENGTH) {
		set_error(e, USB_ENUM_ERR_SHORT_DESCRIPTOR);
		return -1;
	}
	if (descriptor_buf[1] != USB_DESCRIPTOR_TYPE_DEVICE) {
		set_error(e, USB_ENUM_ERR_BAD_DESCRIPTOR_TYPE);
		return -1;
	}

	ret = e->ops->control_read(e->ctx, 0,
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

	ret = e->ops->set_address(e->ctx, USB_ENUM_DEVICE_ADDRESS,
				   USB_ENUM_SET_ADDRESS_TIMEOUT_MS);
	if (ret != 0) {
		set_error(e, classify_xfer_error(ret));
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
