/* SPDX-License-Identifier: Apache-2.0 */
#ifndef STM32_HCD_ADAPTER_H_
#define STM32_HCD_ADAPTER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int stm32_hcd_adapter_id_pin_level(void);
int stm32_hcd_adapter_id_status(void);
uint32_t stm32_hcd_adapter_port_status(void);

/*
 * usb_debug.md Step 30 (plan item 3): minimal bare-metal control-transfer
 * test. Bypasses ST's USBH middleware and its class/enumeration state
 * machine entirely, calling the low-level HAL LL_USB channel primitives
 * (USB_HC_Init/USB_HC_StartXfer) directly to submit a single hand-crafted
 * GET_DESCRIPTOR(DEVICE) SETUP packet to address 0, endpoint 0. Isolates
 * whether the STALL/no-response symptom is a HAL/hardware-level behavior
 * or specific to the middleware's state machine/URB handling.
 */
int stm32_hcd_adapter_raw_control_test(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_HCD_ADAPTER_H_ */
