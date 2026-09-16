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

	return HAL_HCD_Start(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
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
