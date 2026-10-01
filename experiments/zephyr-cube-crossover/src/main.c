/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include "usbh_core.h"
#ifdef CROSSOVER_BOOT_MOUSE_CLASS
#include "usb_boot_mouse.h"
extern USBH_ClassTypeDef usb_boot_mouse_class;
#else
#include "usbh_hid.h"
#endif
#ifdef CROSSOVER_ZEPHYR_BRIDGE
#include "stm32_hcd_adapter.h"
#else
#include "app.h"
#endif

static USBH_HandleTypeDef host;
static volatile uint32_t report_count;
#ifndef CROSSOVER_ZEPHYR_BRIDGE
extern volatile uint32_t cube_mouse_first_irq;
#endif

static void user_event(USBH_HandleTypeDef *phost, uint8_t event)
{
	switch (event) {
	case HOST_USER_CONNECTION:
		printf("Cube crossover: connected\n");
		break;
	case HOST_USER_CLASS_ACTIVE:
		printf("Cube crossover: HID active VID=%04x PID=%04x\n",
		       phost->device.DevDesc.idVendor, phost->device.DevDesc.idProduct);
		break;
	case HOST_USER_DISCONNECTION:
		printf("Cube crossover: disconnected\n");
		break;
	case HOST_USER_UNRECOVERED_ERROR:
		printf("Cube crossover: unrecovered error\n");
		break;
	default:
		break;
	}
}

#ifndef CROSSOVER_BOOT_MOUSE_CLASS
void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
	HID_MOUSE_Info_TypeDef *mouse = USBH_HID_GetMouseInfo(phost);

	if (mouse != NULL) {
		report_count++;
		if (report_count <= 4U || mouse->x != 0U || mouse->y != 0U ||
		    mouse->buttons[0] || mouse->buttons[1] || mouse->buttons[2]) {
			printf("Cube crossover: report=%lu buttons=%u%u%u dx=%d dy=%d\n",
			       (unsigned long)report_count, mouse->buttons[0],
			       mouse->buttons[1], mouse->buttons[2],
			       (int)(int8_t)mouse->x, (int)(int8_t)mouse->y);
		}
	}
}
#endif

int main(void)
{
	printf("Cube middleware host under minimal Zephyr\n");
	if (USBH_Init(&host, user_event, 0U) != USBH_OK ||
#ifndef CROSSOVER_ZEPHYR_BRIDGE
	    hhcd_usb_fs.State != HAL_HCD_STATE_READY ||
#endif
	    USBH_RegisterClass(&host,
#ifdef CROSSOVER_BOOT_MOUSE_CLASS
			       &usb_boot_mouse_class
#else
			       USBH_HID_CLASS
#endif
			       ) != USBH_OK ||
	    USBH_Start(&host) != USBH_OK
#ifndef CROSSOVER_ZEPHYR_BRIDGE
	    ||
	    (USB_OTG_FS->GINTSTS & USB_OTG_GINTSTS_CMOD) == 0U) {
#else
	    ) {
#endif
		printf("Cube crossover: USB host initialization failed\n");
		return -1;
	}

	uint32_t last_status = k_uptime_get_32();
#ifdef CROSSOVER_BURST_CLASS_POLL
	uint8_t fast_poll_count = 0U;
#endif
#ifdef CROSSOVER_ZEPHYR_BRIDGE
	bool channels_printed = false;
#endif

	while (true) {
		(void)USBH_Process(&host);
		uint32_t now = k_uptime_get_32();

		if (now - last_status >= 2000U) {
#ifdef CROSSOVER_BOOT_MOUSE_CLASS
			struct usb_boot_mouse_report report;
			uint32_t count;

			if (usb_boot_mouse_last_report(&report, &count)) {
				report_count = count;
			}
#endif
#ifndef CROSSOVER_ZEPHYR_BRIDGE
			volatile uint32_t *hprt = (volatile uint32_t *)
				((uintptr_t)USB_OTG_FS + USB_OTG_HOST_PORT_BASE);

			printf("Cube crossover: state=%u reports=%lu first_irq=%08lx "
			       "connected=%u port=%u HPRT=%08lx GINTSTS=%08lx GINTMSK=%08lx "
			       "GAHBCFG=%08lx GPIOA=%08lx\n",
			       host.gState, (unsigned long)report_count,
			       (unsigned long)cube_mouse_first_irq,
			       host.device.is_connected, host.device.PortEnabled,
			       (unsigned long)*hprt, (unsigned long)USB_OTG_FS->GINTSTS,
			       (unsigned long)USB_OTG_FS->GINTMSK,
			       (unsigned long)USB_OTG_FS->GAHBCFG,
			       (unsigned long)GPIOA->MODER);
#else
			printf("Cube crossover (Zephyr bridge): state=%u reports=%lu connected=%u port=%u\n",
			       host.gState, (unsigned long)report_count,
			       host.device.is_connected, host.device.PortEnabled);
			if (!channels_printed && host.gState == HOST_CLASS) {
				stm32_hcd_adapter_debug_channels();
				channels_printed = true;
			}
#endif
			last_status = now;
		}
#ifdef CROSSOVER_BURST_CLASS_POLL
		if (++fast_poll_count < 100U) {
			k_busy_wait(50);
		} else {
			fast_poll_count = 0U;
			k_msleep(1);
		}
#elif defined(CROSSOVER_SLOW_CLASS_POLL)
		if (host.gState == HOST_CLASS) {
			k_msleep(1);
		} else {
			k_busy_wait(50);
		}
#else
		k_busy_wait(50);
#endif
	}
}
