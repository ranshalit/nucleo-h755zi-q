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
int stm32_hcd_adapter_id_pin_level(void);
int stm32_hcd_adapter_id_status(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_HCD_ADAPTER_H_ */
