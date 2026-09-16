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
