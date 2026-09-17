/* SPDX-License-Identifier: Apache-2.0 */
#include "stm32_hcd_adapter.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_hcd.h>
#include <stm32h7xx_hal_pwr_ex.h>
#include <stm32h7xx_ll_bus.h>
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_ll_rcc.h>
#include <stm32h7xx_ll_usb.h>

#include "usbh_core.h"
#include "usbh_ioreq.h"
#include "usb_gpio_capture.h"

#define STM32_HCD_IRQ 101 /* OTG_FS_IRQn, RM0468 Table 135 */
#define STM32_HCD_IRQ_PRI 5

static HCD_HandleTypeDef hcd_handle;

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->pData != NULL) {
		(void)USBH_LL_Connect(hhcd->pData);
	}
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->pData != NULL) {
		(void)USBH_LL_Disconnect(hhcd->pData);
	}
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->pData != NULL) {
		USBH_LL_PortEnabled(hhcd->pData);
	}
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->pData != NULL) {
		USBH_LL_PortDisabled(hhcd->pData);
	}
}

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->pData != NULL) {
		USBH_LL_IncTimer(hhcd->pData);
	}
}

static void stm32_hcd_isr(const void *arg)
{
	ARG_UNUSED(arg);
	HAL_HCD_IRQHandler(&hcd_handle);
}

/*
 * Configure PA9 as a no-pull input for VBUS diagnostics, plus PA10
 * (OTG_FS_ID), PA11 (OTG_FS_DM) and PA12 (OTG_FS_DP) for AF10 (OTG_FS),
 * matching UM2408 and the MB1363 D-01 schematic for CN13 USER USB.
 * Without this, PA10-PA12 stay as plain GPIOs and D+/D- are not connected
 * to the OTG_FS peripheral.
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

static int adapter_init_hardware(void)
{
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
	hcd_handle.Init.Sof_enable = ENABLE;
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

	/*
	 * Diagnostic (usb_debug.md Step 28, plan item 1): dump the OTG core's
	 * AHB config and FIFO RAM partitioning right after USB_HostInit()
	 * programs them, for a bit-for-bit diff against the equivalent
	 * Device-mode registers captured from the known-working Step 26
	 * cdc_acm firmware. FIFO RAM is a small fixed shared pool on this
	 * core; a bad RX/NPTX/PTX split is a classic silent-failure mode
	 * (malformed/truncated transfers, no HAL error) that would match our
	 * SETUP-stage STALL symptom.
	 */
	printf("usbhost: GAHBCFG=0x%08lx GRXFSIZ=0x%08lx HNPTXFSIZ=0x%08lx "
	       "HPTXFSIZ=0x%08lx HCFG=0x%08lx\n",
	       (unsigned long)hcd_handle.Instance->GAHBCFG,
	       (unsigned long)hcd_handle.Instance->GRXFSIZ,
	       (unsigned long)hcd_handle.Instance->DIEPTXF0_HNPTXFSIZ,
	       (unsigned long)hcd_handle.Instance->HPTXFSIZ,
	       (unsigned long)((USB_OTG_HostTypeDef *)((uintptr_t)hcd_handle.Instance +
							USB_OTG_HOST_BASE))->HCFG);

	hcd_handle.State = HAL_HCD_STATE_READY;

	IRQ_CONNECT(STM32_HCD_IRQ, STM32_HCD_IRQ_PRI, stm32_hcd_isr, NULL, 0);
	irq_enable(STM32_HCD_IRQ);

	uint32_t hprt = *(__IO uint32_t *)((uintptr_t)hcd_handle.Instance +
					  USB_OTG_HOST_PORT_BASE);
	printf("usbhost: hardware ready HPRT=0x%08lx GINTSTS=0x%08lx GINTMSK=0x%08lx\n",
	       (unsigned long)hprt,
	       (unsigned long)hcd_handle.Instance->GINTSTS,
	       (unsigned long)hcd_handle.Instance->GINTMSK);
	return 0;
}

int stm32_hcd_adapter_id_pin_level(void)
{
	return LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_10) ? 1 : 0;
}

int stm32_hcd_adapter_id_status(void)
{
	return (hcd_handle.Instance->GOTGCTL & USB_OTG_GOTGCTL_CIDSTS) ? 1 : 0;
}

uint32_t stm32_hcd_adapter_port_status(void)
{
	return *(__IO uint32_t *)((uintptr_t)hcd_handle.Instance +
				 USB_OTG_HOST_PORT_BASE);
}

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
	int ret = adapter_init_hardware();

	if (ret != 0) {
		return USBH_FAIL;
	}

	hcd_handle.pData = phost;
	phost->pData = &hcd_handle;
	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_DeInit(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
	HCD_HandleTypeDef *hcd = phost->pData;
	HAL_StatusTypeDef ret = HAL_HCD_Start(hcd);

	/*
	 * Diagnostic (usb_debug.md Step 28, plan item 1): dump GAHBCFG after
	 * HAL_HCD_Start() for a fair, same-lifecycle-stage comparison against
	 * the Device-mode GAHBCFG dump taken right after usb_enable() there.
	 */
	printf("usbhost: after HAL_HCD_Start GAHBCFG=0x%08lx GUSBCFG=0x%08lx\n",
	       (unsigned long)hcd->Instance->GAHBCFG,
	       (unsigned long)hcd->Instance->GUSBCFG);

	return ret == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_Stop(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	switch (HAL_HCD_GetCurrentSpeed(hcd)) {
	case HCD_DEVICE_SPEED_HIGH:
		return USBH_SPEED_HIGH;
	case HCD_DEVICE_SPEED_LOW:
		return USBH_SPEED_LOW;
	default:
		return USBH_SPEED_FULL;
	}
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_ResetPort(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_HC_GetXferCount(hcd, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe,
				    uint8_t epnum, uint8_t dev_address,
				    uint8_t speed, uint8_t ep_type,
				    uint16_t mps)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_HC_Init(hcd, pipe, epnum, dev_address, speed, ep_type, mps) ==
		       HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return HAL_HCD_HC_Halt(hcd, pipe) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	ARG_UNUSED(phost);
	ARG_UNUSED(pipe);
	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe,
				     uint8_t direction, uint8_t ep_type,
				     uint8_t token, uint8_t *buffer,
				     uint16_t length, uint8_t do_ping)
{
	HCD_HandleTypeDef *hcd = phost->pData;
	HAL_StatusTypeDef status;

	if (token == USBH_PID_SETUP && usb_gpio_capture_armed()) {
		/*
		 * Start the tight-loop D+/D- capture immediately before the
		 * hardware is told to transmit the SETUP token, so the
		 * capture window is synchronized with the actual electrical
		 * transaction instead of an arbitrary later snapshot.
		 */
		status = HAL_HCD_HC_SubmitRequest(hcd, pipe, direction, ep_type,
						   token, buffer, length,
						   do_ping);
		usb_gpio_capture_run(USB_GPIO_CAPTURE_MAX_SAMPLES);
		usb_gpio_capture_disarm();
		return status == HAL_OK ? USBH_OK : USBH_FAIL;
	}

	return HAL_HCD_HC_SubmitRequest(hcd, pipe, direction, ep_type, token,
					buffer, length, do_ping) == HAL_OK ?
		       USBH_OK : USBH_FAIL;
}

static void adapter_dump_channel(uint8_t pipe, HCD_URBStateTypeDef state)
{
	uint32_t USBx_BASE = (uint32_t)hcd_handle.Instance;
	USB_OTG_HostChannelTypeDef *hc = USBx_HC(pipe);

	printf("ST USBH: pipe %u URB=%d HC_state=%d HCINT=0x%08lx "
	       "HCINTMSK=0x%08lx HCTSIZ=0x%08lx HCCHAR=0x%08lx "
	       "HPRT=0x%08lx PA9=%lu PG7_nFAULT=%lu\n",
	       pipe, state, HAL_HCD_HC_GetState(&hcd_handle, pipe),
	       (unsigned long)hc->HCINT, (unsigned long)hc->HCINTMSK,
	       (unsigned long)hc->HCTSIZ, (unsigned long)hc->HCCHAR,
	       (unsigned long)stm32_hcd_adapter_port_status(),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOG, LL_GPIO_PIN_7));
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost,
					 uint8_t pipe)
{
	HCD_HandleTypeDef *hcd = phost->pData;
	HCD_URBStateTypeDef state = HAL_HCD_HC_GetURBState(hcd, pipe);

	if ((state == URB_ERROR) || (state == URB_STALL)) {
		adapter_dump_channel(pipe, state);
	}
	return (USBH_URBStateTypeDef)state;
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost,
				      uint8_t state)
{
	ARG_UNUSED(phost);

	if (state != 0U) {
		LL_GPIO_ResetOutputPin(GPIOD, LL_GPIO_PIN_10);
		k_msleep(100);
		if ((stm32_hcd_adapter_port_status() & USB_OTG_HPRT_PCSTS) != 0U) {
			(void)USBH_LL_Connect(hcd_handle.pData);
		}
	} else {
		LL_GPIO_SetOutputPin(GPIOD, LL_GPIO_PIN_10);
	}

	printf("usbhost: VBUS %s PD10=%lu PA9=%lu HPRT=0x%08lx\n",
	       state != 0U ? "on" : "off",
	       (unsigned long)LL_GPIO_IsOutputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9),
	       (unsigned long)stm32_hcd_adapter_port_status());
	return USBH_OK;
}

void stm32_hcd_adapter_vbus_cycle(uint32_t hold_ms)
{
	/*
	 * usb_debug.md Step 32: a genuine VBUS power-off/on, distinct from
	 * a D+/D- port reset (USB_ResetPort()/HAL_HCD_ResetPort()) which
	 * never removes power from the device. After many hours of
	 * aborted/incomplete enumeration attempts in earlier debugging
	 * sessions, the attached device may have wedged internally in a
	 * way only a real power cycle clears - testing that theory here.
	 */
	printf("usbhost: vbus_cycle: forcing VBUS off for %lu ms\n",
	       (unsigned long)hold_ms);
	LL_GPIO_SetOutputPin(GPIOD, LL_GPIO_PIN_10); /* PD10 active-low: set = VBUS off */
	k_msleep(hold_ms);
	printf("usbhost: vbus_cycle: PD10=%lu (should be 1/off), PA9=%lu\n",
	       (unsigned long)LL_GPIO_IsOutputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9));

	LL_GPIO_ResetOutputPin(GPIOD, LL_GPIO_PIN_10); /* PD10 active-low: reset = VBUS on */
	k_msleep(200); /* allow inrush/VBUS ramp to settle before enumerating */
	printf("usbhost: vbus_cycle: VBUS back on, PD10=%lu PA9=%lu HPRT=0x%08lx\n",
	       (unsigned long)LL_GPIO_IsOutputPinSet(GPIOD, LL_GPIO_PIN_10),
	       (unsigned long)LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9),
	       (unsigned long)stm32_hcd_adapter_port_status());
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe,
				     uint8_t toggle)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	if (hcd->hc[pipe].ep_is_in != 0U) {
		hcd->hc[pipe].toggle_in = toggle;
	} else {
		hcd->hc[pipe].toggle_out = toggle;
	}
	return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	HCD_HandleTypeDef *hcd = phost->pData;

	return hcd->hc[pipe].ep_is_in != 0U ?
		       hcd->hc[pipe].toggle_in : hcd->hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t delay)
{
	k_msleep(delay);
}

int stm32_hcd_adapter_raw_control_test(void)
{
	static uint8_t setup_pkt[8] __aligned(4) = {
		0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00,
	}; /* GET_DESCRIPTOR(DEVICE), wLength=64, bmRequestType=IN */
	/*
	 * Use channel 1, not 0: ST's USBH middleware's own control pipe
	 * already occupies channel 0 and keeps retrying in the background
	 * (usb_host_poll_loop() -> USBH_Process()), which would corrupt this
	 * test's results if both drove the same channel concurrently.
	 */
	uint8_t ch = 1;
	HCD_HCTypeDef *hc = &hcd_handle.hc[ch];
	USB_OTG_GlobalTypeDef *USBx = hcd_handle.Instance;
	uint32_t USBx_BASE = (uint32_t)USBx;
	HAL_StatusTypeDef ret;

	if (hcd_handle.State != HAL_HCD_STATE_READY &&
	    hcd_handle.State != HAL_HCD_STATE_BUSY) {
		printf("rawtest: hcd not ready (state=%d)\n", hcd_handle.State);
		return -EAGAIN;
	}

	uint32_t hprt = *(__IO uint32_t *)((uintptr_t)USBx + USB_OTG_HOST_PORT_BASE);

	/*
	 * A/B test against the bare-metal reference revealed HPRT.PENA=0
	 * here (port not enabled), unlike bare-metal which always does an
	 * explicit port reset right before starting a channel transfer.
	 * Starting a channel while the port session is not enabled appears
	 * to make the OTG core auto-halt the channel immediately (CHENA and
	 * CHDIS both latch with HCINT never updating) - exactly this test's
	 * long-standing "stuck channel" symptom. Force a fresh port reset
	 * here so this test matches bare-metal's sequence exactly.
	 */
	if (((hprt >> 2) & 0x1U) == 0U) {
		printf("rawtest: HPRT.PENA=0 (0x%08lx), forcing port reset\n",
		       (unsigned long)hprt);
		(void)HAL_HCD_ResetPort(&hcd_handle);
		k_msleep(50);
		hprt = *(__IO uint32_t *)((uintptr_t)USBx + USB_OTG_HOST_PORT_BASE);
		printf("rawtest: HPRT after reset = 0x%08lx\n", (unsigned long)hprt);
	}

	printf("rawtest: RCC->CR HSI48ON=%lu HSI48RDY=%lu, D2CCIP2R.USBSEL=%lu "
	       "(0=disable,1=PLL1Q,2=PLL3Q,3=HSI48 - want 3), AHB1ENR.USB2OTGHSEN=%lu\n",
	       (unsigned long)((RCC->CR & RCC_CR_HSI48ON) != 0),
	       (unsigned long)((RCC->CR & RCC_CR_HSI48RDY) != 0),
	       (unsigned long)((RCC->D2CCIP2R & RCC_D2CCIP2R_USBSEL) >>
			       RCC_D2CCIP2R_USBSEL_Pos),
	       (unsigned long)((RCC->AHB1ENR & RCC_AHB1ENR_USB2OTGHSEN) != 0));
	{
		/*
		 * Disable the OTG_FS NVIC IRQ for the duration of this
		 * diagnostic block. HAL_HCD_IRQHandler() (registered and
		 * enabled at init) services GINTSTS.SOF whenever
		 * GINTMSK.SOFM is set and clears it in the ISR -- if that's
		 * happening, our own busy-poll reads below would always see
		 * 0 (the ISR always wins the race), producing a false
		 * "SOF is dead" conclusion even if SOF is really firing.
		 * Disabling the IRQ here removes that ambiguity: any
		 * remaining "0 reasserts" result after this point cannot be
		 * explained by ISR interference.
		 */
		irq_disable(STM32_HCD_IRQ);
		printf("rawtest: OTG_FS IRQ disabled for diagnostic window (was it stealing "
		       "SOF flag before our poll could see it?)\n");

		USB_OTG_HostTypeDef *host_regs =
			(USB_OTG_HostTypeDef *)((uintptr_t)USBx + USB_OTG_HOST_BASE);
		uint32_t hfnum_a = host_regs->HFNUM;

		k_msleep(2);
		uint32_t hfnum_b = host_regs->HFNUM;

		printf("rawtest: HFNUM before=0x%08lx after 2ms=0x%08lx "
		       "(frame number field should have incremented if SOF/PHY clock is alive)\n",
		       (unsigned long)hfnum_a, (unsigned long)hfnum_b);

		/*
		 * Sample FRNUM (bits 15:0 of HFNUM) repeatedly over a longer
		 * window to conclusively determine whether the host core is
		 * generating Start-Of-Frame events at all (required every 1ms
		 * in Full Speed for the core's internal transfer scheduler to
		 * ever inject a non-periodic/control transfer onto the bus).
		 */
		uint16_t frnum_first = (uint16_t)(host_regs->HFNUM & 0xFFFFU);
		uint16_t frnum_last = frnum_first;
		int frnum_changed = 0;

		for (int i = 0; i < 20; i++) {
			k_msleep(1);
			uint16_t f = (uint16_t)(host_regs->HFNUM & 0xFFFFU);

			if (f != frnum_last) {
				frnum_changed++;
			}
			frnum_last = f;
		}
		printf("rawtest: FRNUM over 20ms: first=%u last=%u changed=%d times "
		       "(0 changes = SOF/scheduler NOT running - core cannot inject transfers)\n",
		       frnum_first, frnum_last, frnum_changed);
		printf("rawtest: HFIR=0x%08lx (expected PHY-clock-cycles-per-1ms, FS ~48000)\n",
		       (unsigned long)host_regs->HFIR);

		/*
		 * Experiment: bare-metal reference never re-enters the
		 * HPRT PENCHNG interrupt path that HAL's USB_InitFSLSPClkSel()
		 * uses to rewrite HFIR to 48000; it stays at the silicon
		 * reset default of 60000 and SOF/FRNUM cadence is correct
		 * (~1ms) there. Force HFIR back to 60000 here and re-sweep
		 * FRNUM to test whether the 48000 value (or the act of
		 * dynamically rewriting HFIR) is what stalls the SOF/frame
		 * scheduler under Zephyr.
		 */
		host_regs->HFIR = 60000U;
		uint16_t frnum2_first = (uint16_t)(host_regs->HFNUM & 0xFFFFU);
		uint16_t frnum2_last = frnum2_first;
		int frnum2_changed = 0;

		for (int i = 0; i < 20; i++) {
			k_msleep(1);
			uint16_t f = (uint16_t)(host_regs->HFNUM & 0xFFFFU);

			if (f != frnum2_last) {
				frnum2_changed++;
			}
			frnum2_last = f;
		}
		printf("rawtest: after forcing HFIR=60000: FRNUM over 20ms: first=%u last=%u "
		       "changed=%d times (HFIR now 0x%08lx)\n",
		       frnum2_first, frnum2_last, frnum2_changed,
		       (unsigned long)host_regs->HFIR);

		/*
		 * PCGCCTL (Power and Clock Gating Control) lives at offset
		 * 0xE00 from the USB_OTG_FS base, right after the FIFO
		 * region. Bit0 = STPPCLK (stop PHY clock). If set, the PHY
		 * clock feeding the frame timer/SOF generator is gated off
		 * entirely, which would explain both a frozen FRNUM and zero
		 * D+/D- electrical activity regardless of HFIR's value.
		 */
		volatile uint32_t *pcgcctl = (volatile uint32_t *)(((uint32_t)USB_OTG_FS) + 0xE00U);
		printf("rawtest: PCGCCTL=0x%08lx (bit0=STPPCLK bit1=GATEHCLK)\n",
		       (unsigned long)*pcgcctl);

		/*
		 * Direct SOF interrupt-flag test, independent of FRNUM
		 * register reads: clear GINTSTS.SOF (bit3, W1C) repeatedly
		 * and count how many times it re-asserts itself within a
		 * fixed window. If the core is truly generating SOF every
		 * 1ms, this should re-assert ~5 times in 5ms regardless of
		 * whether our FRNUM decode is somehow wrong.
		 */
		USB_OTG_GlobalTypeDef *greg = (USB_OTG_GlobalTypeDef *)USB_OTG_FS;
		int sof_count = 0;

		greg->GINTSTS = USB_OTG_GINTSTS_SOF;
		for (int i = 0; i < 5000; i++) {
			if (greg->GINTSTS & USB_OTG_GINTSTS_SOF) {
				sof_count++;
				greg->GINTSTS = USB_OTG_GINTSTS_SOF;
			}
			k_busy_wait(1);
		}
		printf("rawtest: SOF interrupt flag reasserted %d times in ~5ms busy-poll window\n",
		       sof_count);

		/*
		 * Test whether the Ethernet MAC's AHB clock enables
		 * (ETH1MACEN/ETH1TXEN/ETH1RXEN, bits 15-17 of AHB1ENR) are
		 * starving the OTG core's frame timer via AHB bus/arbitration
		 * contention -- the only significant extra AHB master this
		 * app has that the bare-metal reference doesn't. Gate them
		 * off, re-run the same SOF reassert-count probe, then
		 * restore them (networking may glitch briefly but must not
		 * be left disabled).
		 */
		uint32_t ahb1enr_saved = RCC->AHB1ENR;
		uint32_t eth_mask = RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN |
				    RCC_AHB1ENR_ETH1RXEN;

		RCC->AHB1ENR = ahb1enr_saved & ~eth_mask;
		__DSB();

		greg->GINTSTS = USB_OTG_GINTSTS_SOF;
		int sof_count_eth_off = 0;

		for (int i = 0; i < 5000; i++) {
			if (greg->GINTSTS & USB_OTG_GINTSTS_SOF) {
				sof_count_eth_off++;
				greg->GINTSTS = USB_OTG_GINTSTS_SOF;
			}
			k_busy_wait(1);
		}
		RCC->AHB1ENR = ahb1enr_saved;
		__DSB();
		printf("rawtest: with Ethernet AHB clocks (MAC/TX/RX) gated OFF: SOF interrupt "
		       "flag reasserted %d times in ~5ms (Ethernet clocks restored after test)\n",
		       sof_count_eth_off);

		/*
		 * Dual-core H745/H755 sanity check: if the CM4 core is
		 * configured to auto-boot (FLASH option byte BCM4) and is
		 * running some independent firmware, it could contend for
		 * this shared USB_OTG_FS peripheral or its clock/reset
		 * domain. Confirm it's NOT set (CM7-only boot) so this isn't
		 * a silent dual-core conflict.
		 */
		printf("rawtest: FLASH->OPTSR_CUR=0x%08lx BCM4=%lu (1=CM4 auto-boots at reset, "
		       "0=CM7-only)\n",
		       (unsigned long)FLASH->OPTSR_CUR,
		       (unsigned long)((FLASH->OPTSR_CUR & FLASH_OPTSR_BCM4) != 0));

		irq_enable(STM32_HCD_IRQ);
		printf("rawtest: OTG_FS IRQ re-enabled\n");
	}
	printf("rawtest: start (HPRT=0x%08lx)\n", (unsigned long)hprt);
	printf("rawtest: clocks SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu\n",
	       (unsigned long)HAL_RCC_GetSysClockFreq(),
	       (unsigned long)HAL_RCC_GetHCLKFreq(),
	       (unsigned long)HAL_RCC_GetPCLK1Freq(),
	       (unsigned long)HAL_RCC_GetPCLK2Freq());
	printf("rawtest: GAHBCFG=0x%08lx GUSBCFG=0x%08lx GCCFG=0x%08lx "
	       "GRXFSIZ=0x%08lx HNPTXFSIZ=0x%08lx HPTXFSIZ=0x%08lx HCFG=0x%08lx\n",
	       (unsigned long)USBx->GAHBCFG,
	       (unsigned long)USBx->GUSBCFG,
	       (unsigned long)USBx->GCCFG,
	       (unsigned long)USBx->GRXFSIZ,
	       (unsigned long)USBx->DIEPTXF0_HNPTXFSIZ,
	       (unsigned long)USBx->HPTXFSIZ,
	       (unsigned long)((USB_OTG_HostTypeDef *)((uintptr_t)USBx +
							USB_OTG_HOST_BASE))->HCFG);
	extern uint32_t SystemCoreClock;
	printf("rawtest: SystemCoreClock=%lu SCB->CPACR=0x%08lx SCB->CCR=0x%08lx\n",
	       (unsigned long)SystemCoreClock,
	       (unsigned long)SCB->CPACR,
	       (unsigned long)SCB->CCR);

	/*
	 * Pausing usb_host_poll_loop() only stops software from calling
	 * USBH_Process() again - it does NOT touch hardware channel 0,
	 * which may still be sitting armed (CHENA=1) from the last
	 * USBH_Process() iteration before the pause took effect. An armed
	 * channel 0 retransmitting SETUP/IN tokens on the shared D+/D-
	 * lines at the same time channel 1 tries to transact could corrupt
	 * the bus and produce a spurious TXERR that has nothing to do with
	 * channel 1's own config. Explicitly halt channel 0 here so this
	 * test's bus conditions match bare-metal's (zero other channels
	 * ever active).
	 */
	printf("rawtest: HCCHAR(ch0) before halt = 0x%08lx\n",
	       (unsigned long)USBx_HC(0)->HCCHAR);
	if ((USBx_HC(0)->HCCHAR & USB_OTG_HCCHAR_CHENA) != 0U) {
		(void)HAL_HCD_HC_Halt(&hcd_handle, 0);
		k_msleep(5);
	}
	printf("rawtest: HCCHAR(ch0) after halt = 0x%08lx\n",
	       (unsigned long)USBx_HC(0)->HCCHAR);

	hc->dev_addr = 0;
	hc->ch_num = ch;
	hc->ep_num = 0;
	hc->ep_is_in = 0; /* SETUP stage is always OUT */
	hc->speed = (uint8_t)USB_GetHostSpeed(USBx);
	hc->do_ping = 0;
	hc->do_ssplit = 0;
	hc->do_csplit = 0;
	hc->ep_type = EP_TYPE_CTRL;
	hc->max_packet = 64;
	hc->data_pid = HC_PID_SETUP;
	hc->xfer_buff = setup_pkt;
	hc->xfer_len = sizeof(setup_pkt);
	hc->toggle_in = 0;
	hc->toggle_out = 0;

	ret = USB_HC_Init(USBx, ch, 0, 0, hc->speed, EP_TYPE_CTRL, 64);
	printf("rawtest: USB_HC_Init -> %d HCCHAR=0x%08lx HCINTMSK=0x%08lx\n",
	       ret, (unsigned long)USBx_HC(ch)->HCCHAR,
	       (unsigned long)USBx_HC(ch)->HCINTMSK);
	if (ret != HAL_OK) {
		return -EIO;
	}

	printf("rawtest: before StartXfer HCCHAR=0x%08lx HCTSIZ=0x%08lx "
	       "HCINT=0x%08lx GINTSTS=0x%08lx\n",
	       (unsigned long)USBx_HC(ch)->HCCHAR,
	       (unsigned long)USBx_HC(ch)->HCTSIZ,
	       (unsigned long)USBx_HC(ch)->HCINT,
	       (unsigned long)USBx->GINTSTS);

	/*
	 * Disable the shared OTG_FS IRQ for the duration of the poll loop
	 * *before* starting the transfer, not after. stm32_hcd_isr() ->
	 * HAL_HCD_IRQHandler() services ALL channels (including channel 0,
	 * still being retried in the background by ST's USBH middleware/poll
	 * thread) and contains its own NAK/retry state machine that can
	 * silently re-arm/rewrite channel registers out from under this
	 * "bare-metal" test. Disabling the IRQ here means any register
	 * change we observe below is a raw hardware transition, not a side
	 * effect of HAL's own software retry logic.
	 *
	 * Root cause found by A/B testing against a real bare-metal
	 * (non-Zephyr) reference: disabling the IRQ *after* calling
	 * USB_HC_StartXfer() left a race window where, if the SETUP
	 * transaction completed fast enough, HAL_HCD_IRQHandler() would run
	 * once, see XFRC, clear HCINT (write-1-to-clear) and issue a
	 * channel-halt request (CHDIS) that requires a *second* interrupt
	 * (CHH) to finish disabling the channel. Disabling the IRQ right
	 * after that first service, before the CHH interrupt could ever
	 * arrive, permanently froze the channel with CHENA+CHDIS both set
	 * and HCINT stuck at 0 - exactly the "stuck host channel" symptom
	 * chased throughout this investigation. Moving irq_disable() to
	 * before USB_HC_StartXfer() removes the race entirely, matching
	 * the bare-metal reference which never enables the NVIC IRQ at all.
	 */
	irq_disable(STM32_HCD_IRQ);

	/*
	 * Capture D+/D- immediately around the actual channel start, to
	 * correlate against the production-path capture (usb_debug.md
	 * Step 33) that showed zero electrical transitions during a
	 * TXERR-failing SETUP submission. If this rawtest path -- which
	 * bit-for-bit matches the bare-metal reference and is known to
	 * report TXERR under Zephyr -- ALSO shows zero transitions, that
	 * means the OTG core is reporting TXERR without ever physically
	 * driving the bus, i.e. the fault is upstream of the pin drivers
	 * (FIFO push / serializer), not a GPIO/pinmux issue (already ruled
	 * out separately via `usbhost gpio`).
	 */
	usb_gpio_capture_arm();

	ret = USB_HC_StartXfer(USBx, hc, 0 /* no DMA */);
	printf("rawtest: USB_HC_StartXfer -> %d\n", ret);

	if (usb_gpio_capture_armed()) {
		usb_gpio_capture_run(USB_GPIO_CAPTURE_MAX_SAMPLES);
		usb_gpio_capture_disarm();
	}
	{
		const uint32_t *cbuf;
		uint32_t ccount, ccycles, transitions = 0;

		usb_gpio_capture_get(&cbuf, &ccount, &ccycles);
		if (ccount > 0U) {
			uint32_t prev = cbuf[0] & ((1U << 11) | (1U << 12));

			for (uint32_t i = 1; i < ccount; i++) {
				uint32_t cur = cbuf[i] & ((1U << 11) | (1U << 12));

				if (cur != prev) {
					transitions++;
					prev = cur;
				}
			}
			printf("rawtest: dpdm capture: samples=%u cycles=%u "
			       "transitions=%u initial DP=%u DM=%u\n",
			       (unsigned)ccount, (unsigned)ccycles,
			       (unsigned)transitions,
			       (unsigned)((cbuf[0] >> 12) & 1U),
			       (unsigned)((cbuf[0] >> 11) & 1U));
		} else {
			printf("rawtest: dpdm capture: no samples (arm/trigger mismatch)\n");
		}
	}

	for (int i = 0; i < 100; i++) {
		uint32_t hcint = USBx_HC(ch)->HCINT;
		uint32_t hcchar = USBx_HC(ch)->HCCHAR;
		uint32_t hctsiz = USBx_HC(ch)->HCTSIZ;

		printf("rawtest: poll[%02d] HCINT=0x%08lx HCCHAR=0x%08lx "
		       "HCTSIZ=0x%08lx GINTSTS=0x%08lx\n",
		       i, (unsigned long)hcint, (unsigned long)hcchar,
		       (unsigned long)hctsiz, (unsigned long)USBx->GINTSTS);

		if (hcint != 0U) {
			printf("rawtest: HCINT went non-zero after %d polls, stopping\n", i);
			break;
		}
		k_msleep(2);
	}

	irq_enable(STM32_HCD_IRQ);
	printf("rawtest: done\n");
	return 0;
}
