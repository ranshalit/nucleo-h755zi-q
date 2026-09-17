/* SPDX-License-Identifier: Apache-2.0 */
#ifndef STM32_HCD_ADAPTER_H_
#define STM32_HCD_ADAPTER_H_

#include <stdbool.h>
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

/*
 * usb_debug.md Step 32: hard power-cycle VBUS (PD10) for hold_ms
 * milliseconds, then re-enable it. Used to test whether a genuine VBUS
 * power-off/on (not just a D+/D- port reset) recovers a USB device that
 * may have wedged internally after many prior aborted/incomplete
 * enumeration attempts without ever actually losing power.
 */
void stm32_hcd_adapter_vbus_cycle(uint32_t hold_ms);

/*
 * usb_debug.md Step 38: switch between interrupt-driven servicing (the
 * normal production mode, where HAL_HCD_IRQHandler() runs asynchronously
 * from the OTG_FS NVIC ISR) and bare-metal-style synchronous polling
 * (NVIC IRQ permanently disabled; the caller must instead invoke
 * stm32_hcd_adapter_poll_service() repeatedly from a normal thread
 * context, exactly like ST's own bare-metal superloop reference does).
 * This exists to test whether the camera-enumeration failure is an
 * artifact of running the HCD state machine under async interrupt
 * preemption/scheduling jitter, since bare-metal (which never enables
 * this IRQ at all) succeeds 100% of the time on identical hardware.
 */
void stm32_hcd_adapter_set_poll_mode(bool enable);
bool stm32_hcd_adapter_get_poll_mode(void);
void stm32_hcd_adapter_poll_service(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_HCD_ADAPTER_H_ */
