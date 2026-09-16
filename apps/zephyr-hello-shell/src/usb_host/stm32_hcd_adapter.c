/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_hcd.h>
#include <stm32h7xx_hal_pwr_ex.h>
#include <stm32h7xx_ll_bus.h>
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_ll_rcc.h>
#include <stm32h7xx_ll_usb.h>

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
static uint16_t control_mps = STM32_HCD_CONTROL_MPS;

static void adapter_dump_channel(const char *stage, HCD_HandleTypeDef *hcd)
{
	uint32_t USBx_BASE = (uint32_t)hcd->Instance;
	USB_OTG_HostChannelTypeDef *hc = USBx_HC(STM32_HCD_CHANNEL_CONTROL);

	printf("usbhost: %s failed HC_state=%d URB=%d HCINT=0x%08lx "
	       "HCINTMSK=0x%08lx HCTSIZ=0x%08lx HCCHAR=0x%08lx HPRT=0x%08lx "
	       "PA9=%lu PG7_nFAULT=%lu\n",
	       stage,
	       HAL_HCD_HC_GetState(hcd, STM32_HCD_CHANNEL_CONTROL),
	       HAL_HCD_HC_GetURBState(hcd, STM32_HCD_CHANNEL_CONTROL),
	       (unsigned long)hc->HCINT,
	       (unsigned long)hc->HCINTMSK,
	       (unsigned long)hc->HCTSIZ,
	       (unsigned long)hc->HCCHAR,
	       (unsigned long)USBx_HPRT0,
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOG, LL_GPIO_PIN_7));
}

static int adapter_submit_stage(HCD_HandleTypeDef *hcd, uint8_t direction,
				uint8_t token, uint8_t *buf, uint16_t length,
				int64_t deadline, const char *stage)
{
	while (k_uptime_get() <= deadline) {
		if (HAL_HCD_HC_SubmitRequest(hcd, STM32_HCD_CHANNEL_CONTROL,
					     direction, EP_TYPE_CTRL, token,
					     buf, length, 0) != HAL_OK) {
			return -EPROTO;
		}

		while (k_uptime_get() <= deadline) {
			HCD_URBStateTypeDef urb =
				HAL_HCD_HC_GetURBState(hcd, STM32_HCD_CHANNEL_CONTROL);

			if (urb == URB_DONE) {
				return 0;
			}
			if (urb == URB_STALL) {
				adapter_dump_channel(stage, hcd);
				return -EPIPE;
			}
			if (urb == URB_ERROR) {
				adapter_dump_channel(stage, hcd);
				return -EPROTO;
			}
			if ((urb == URB_NOTREADY) || (urb == URB_NYET)) {
				break;
			}
			k_msleep(1);
		}
	}

	adapter_dump_channel(stage, hcd);
	return -ETIMEDOUT;
}

static int adapter_reset_port(void *ctx)
{
	HCD_HandleTypeDef *hcd = ctx;

	/* Match STM32 USB Host middleware attachment/reset settling times. */
	k_msleep(100);
	if (HAL_HCD_ResetPort(hcd) != HAL_OK) {
		return -1;
	}
	k_msleep(100);
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
	int ret;

	setup_pkt[0] = bmRequestType;
	setup_pkt[1] = bRequest;
	setup_pkt[2] = (uint8_t)(wValue & 0xFF);
	setup_pkt[3] = (uint8_t)(wValue >> 8);
	setup_pkt[4] = (uint8_t)(wIndex & 0xFF);
	setup_pkt[5] = (uint8_t)(wIndex >> 8);
	setup_pkt[6] = (uint8_t)(length & 0xFF);
	setup_pkt[7] = (uint8_t)(length >> 8);

	(void)HAL_HCD_HC_Init(hcd, STM32_HCD_CHANNEL_CONTROL, 0, address,
			      HCD_DEVICE_SPEED_FULL, EP_TYPE_CTRL, control_mps);

	deadline = k_uptime_get() + timeout_ms;
	ret = adapter_submit_stage(hcd, 0, 0, setup_pkt, sizeof(setup_pkt),
				   deadline, "setup");
	if (ret != 0) {
		return ret;
	}

	ret = adapter_submit_stage(hcd, 1, 1, buf, length, deadline, "data-in");
	if (ret != 0) {
		return ret;
	}

	uint16_t received = (uint16_t)HAL_HCD_HC_GetXferCount(
		hcd, STM32_HCD_CHANNEL_CONTROL);

	ret = adapter_submit_stage(hcd, 0, 1, NULL, 0, deadline, "status-out");
	if (ret != 0) {
		return ret;
	}

	if ((address == 0U) && (bRequest == 0x06U) &&
	    ((wValue >> 8) == 0x01U) && (received >= 8U)) {
		switch (buf[7]) {
		case 8:
		case 16:
		case 32:
		case 64:
			control_mps = buf[7];
			break;
		default:
			control_mps = 8U;
			break;
		}
	}

	return received;
}

static int adapter_set_address(void *ctx, uint8_t address, int32_t timeout_ms)
{
	HCD_HandleTypeDef *hcd = ctx;
	uint8_t setup_pkt[8] = {
		0x00, 0x05, address, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	int64_t deadline;
	int ret;

	(void)HAL_HCD_HC_Init(hcd, STM32_HCD_CHANNEL_CONTROL, 0, 0,
			      HCD_DEVICE_SPEED_FULL, EP_TYPE_CTRL,
			      control_mps);

	deadline = k_uptime_get() + timeout_ms;
	ret = adapter_submit_stage(hcd, 0, 0, setup_pkt, sizeof(setup_pkt),
				   deadline, "set-address-setup");
	if (ret != 0) {
		return ret;
	}

	ret = adapter_submit_stage(hcd, 1, 1, NULL, 0, deadline,
				   "set-address-status-in");
	if (ret == 0) {
		k_msleep(2);
	}
	return ret;
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

/*
 * Configure PA9 as a no-pull input for VBUS diagnostics, plus PA10
 * (OTG_FS_ID), PA11 (OTG_FS_DM) and PA12 (OTG_FS_DP) for AF10 (OTG_FS),
 * matching the pinout in
 * docs/rm0477-pinmux-pad-extract.md and docs/um2407-...md Table 16 (CN13
 * "USER USB" connector). Without this, PA10-PA12 stay as plain GPIOs and
 * D+/D- are never connected to the OTG_FS PHY. ID currently reads high,
 * but that is not yet established as the cause of the forced-mode failure.
 */
static void adapter_config_gpio(void)
{
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOG);

	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_INPUT);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_9, LL_GPIO_PULL_NO);

	LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_10, LL_GPIO_AF_10);
	LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_11, LL_GPIO_AF_10);
	LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_12, LL_GPIO_AF_10);

	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_11, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_12, LL_GPIO_MODE_ALTERNATE);

	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_11, LL_GPIO_SPEED_FREQ_VERY_HIGH);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_12, LL_GPIO_SPEED_FREQ_VERY_HIGH);

	LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_10, LL_GPIO_OUTPUT_OPENDRAIN);
	LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_11, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_12, LL_GPIO_OUTPUT_PUSHPULL);

	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_10, LL_GPIO_PULL_UP);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_11, LL_GPIO_PULL_NO);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_12, LL_GPIO_PULL_NO);

	LL_GPIO_SetPinMode(GPIOG, LL_GPIO_PIN_7, LL_GPIO_MODE_INPUT);
	LL_GPIO_SetPinPull(GPIOG, LL_GPIO_PIN_7, LL_GPIO_PULL_NO);
}

int stm32_hcd_adapter_init(void)
{
	usb_enumerator_init(&enumerator, &stm32_ops, &hcd_handle);
	control_mps = STM32_HCD_CONTROL_MPS;

	printf("usbhost: init start\n");
	adapter_config_gpio();
	printf("usbhost: gpio configured\n");
	printf("usbhost: PD10 VBUS_EN ODR=%lu IDR=%lu, PA9 VBUS sense=%lu\n",
	       (unsigned long)LL_GPIO_IsOutputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9));
	k_msleep(20);
	printf("usbhost: VBUS settled PD10=%lu PA9=%lu\n",
	       (unsigned long)LL_GPIO_IsOutputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9));

	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_USB2OTGHS);

	/*
	 * The OTG_FS embedded full-speed PHY needs a 48 MHz USB kernel clock.
	 * Nothing else in this app configures it (no USB device stack is
	 * enabled), so select the always-available internal HSI48 oscillator.
	 */
	LL_RCC_HSI48_Enable();
	for (int i = 0; !LL_RCC_HSI48_IsReady(); i++) {
		if (i > 1000) {
			printf("usbhost: HSI48 clock never became ready\n");
			return -ENODEV;
		}
		k_busy_wait(100);
	}
	LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);
	printf("usbhost: HSI48 clock ready\n");

	/*
	 * STM32H7 requires the internal USB 3.3V supply/analog domain to be
	 * enabled before the OTG core can complete reset; otherwise
	 * HAL_HCD_Init() times out waiting for GRSTCTL_AHBIDL.
	 * See RM0468 "USB power supplies" and HAL_PWREx_EnableUSBVoltageDetector().
	 */
	HAL_PWREx_EnableUSBVoltageDetector();
	for (int i = 0; !__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY); i++) {
		if (i > 1000) {
			printf("usbhost: USB33RDY never set; power domain not ready\n");
			return -ENODEV;
		}
		k_busy_wait(100);
	}
	printf("usbhost: USB33RDY ready\n");

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

	HAL_StatusTypeDef hal_ret;

	hcd_handle.State = HAL_HCD_STATE_RESET;

	/*
	 * Diagnostic: read the connector-ID status directly (GOTGCTL.CIDSTS)
	 * before forcing host mode. 0 = A-device (ID pin grounded, host-side
	 * plug); 1 = B-device (ID pin floating/high, device-side plug). This
	 * records the physical ID state while separately checking whether the
	 * software force-host bit latches and whether CMOD follows it.
	 */
	printf("usbhost: GOTGCTL=0x%08lx (CIDSTS=%lu, PA10_IDR=%lu, %s)\n",
	       (unsigned long)hcd_handle.Instance->GOTGCTL,
	       (unsigned long)((hcd_handle.Instance->GOTGCTL & USB_OTG_GOTGCTL_CIDSTS) != 0),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_10),
	       (hcd_handle.Instance->GOTGCTL & USB_OTG_GOTGCTL_CIDSTS) ?
		       "B-device/ID-high/NOT-grounded" : "A-device/ID-low/grounded");

	printf("usbhost: before CoreInit GUSBCFG=0x%08lx GCCFG=0x%08lx GRSTCTL=0x%08lx\n",
	       (unsigned long)hcd_handle.Instance->GUSBCFG,
	       (unsigned long)hcd_handle.Instance->GCCFG,
	       (unsigned long)hcd_handle.Instance->GRSTCTL);
	hal_ret = USB_CoreInit(hcd_handle.Instance, hcd_handle.Init);
	printf("usbhost: USB_CoreInit -> %d (GUSBCFG=0x%08lx GCCFG=0x%08lx GRSTCTL=0x%08lx)\n",
	       hal_ret,
	       (unsigned long)hcd_handle.Instance->GUSBCFG,
	       (unsigned long)hcd_handle.Instance->GCCFG,
	       (unsigned long)hcd_handle.Instance->GRSTCTL);
	if (hal_ret != HAL_OK) {
		hcd_handle.State = HAL_HCD_STATE_ERROR;
		return -EIO;
	}

	hal_ret = USB_SetCurrentMode(hcd_handle.Instance, USB_HOST_MODE);
	printf("usbhost: USB_SetCurrentMode -> %d (GUSBCFG=0x%08lx FHMOD=%lu FDMOD=%lu "
	       "GINTSTS=0x%08lx CMOD=%lu)\n",
	       hal_ret,
	       (unsigned long)hcd_handle.Instance->GUSBCFG,
	       (unsigned long)((hcd_handle.Instance->GUSBCFG &
				USB_OTG_GUSBCFG_FHMOD) != 0),
	       (unsigned long)((hcd_handle.Instance->GUSBCFG &
				USB_OTG_GUSBCFG_FDMOD) != 0),
	       (unsigned long)hcd_handle.Instance->GINTSTS,
	       (unsigned long)((hcd_handle.Instance->GINTSTS &
				USB_OTG_GINTSTS_CMOD) != 0));
	if (hal_ret != HAL_OK) {
		hcd_handle.State = HAL_HCD_STATE_ERROR;
		return -EIO;
	}

	hal_ret = USB_HostInit(hcd_handle.Instance, hcd_handle.Init);
	printf("usbhost: USB_HostInit -> %d\n", hal_ret);
	if (hal_ret != HAL_OK) {
		hcd_handle.State = HAL_HCD_STATE_ERROR;
		return -EIO;
	}

	hcd_handle.State = HAL_HCD_STATE_READY;

	IRQ_CONNECT(STM32_HCD_IRQ, STM32_HCD_IRQ_PRI, stm32_hcd_isr, NULL, 0);
	irq_enable(STM32_HCD_IRQ);

	hal_ret = HAL_HCD_Start(&hcd_handle);
	if (hal_ret != HAL_OK) {
		printf("usbhost: HAL_HCD_Start failed, status=%d\n", hal_ret);
		return -EIO;
	}

	/* MB1363B U18 VBUS switch enable is active-low. */
	LL_GPIO_ResetOutputPin(GPIOD, LL_GPIO_PIN_10);
	k_msleep(100);
	uint32_t hprt = *(__IO uint32_t *)((uintptr_t)hcd_handle.Instance +
					  USB_OTG_HOST_PORT_BASE);
	if ((hprt & USB_OTG_HPRT_PCSTS) != 0U) {
		atomic_or(&pending_events, EVT_CONNECT);
	}
	printf("usbhost: ready HPRT=0x%08lx GINTSTS=0x%08lx GINTMSK=0x%08lx\n",
	       (unsigned long)hprt,
	       (unsigned long)hcd_handle.Instance->GINTSTS,
	       (unsigned long)hcd_handle.Instance->GINTMSK);
	return 0;
}

struct usb_enumerator *stm32_hcd_adapter_enumerator(void)
{
	return &enumerator;
}

int stm32_hcd_adapter_id_pin_level(void)
{
	return LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_10) ? 1 : 0;
}

int stm32_hcd_adapter_id_status(void)
{
	return (hcd_handle.Instance->GOTGCTL & USB_OTG_GOTGCTL_CIDSTS) ? 1 : 0;
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
