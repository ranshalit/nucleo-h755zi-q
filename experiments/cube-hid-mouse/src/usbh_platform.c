#include "app.h"
#include "usbh_core.h"

#ifdef CUBE_ZEPHYR_CROSSOVER
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <stm32h7xx_ll_rcc.h>
#include <stm32h7xx_ll_usb.h>

volatile uint32_t cube_mouse_first_irq;

static void cube_usb_isr(const void *arg)
{
  uint32_t USBx_BASE = (uint32_t)USB_OTG_FS;
  uint32_t irq = USBx_HC(2)->HCINT & USBx_HC(2)->HCINTMSK;

  (void)arg;
  if (irq != 0U && cube_mouse_first_irq == 0U)
  {
    cube_mouse_first_irq = irq;
  }
  HAL_HCD_IRQHandler(&hhcd_usb_fs);
}
#endif

HCD_HandleTypeDef hhcd_usb_fs;

void HAL_HCD_MspInit(HCD_HandleTypeDef *hhcd)
{
  GPIO_InitTypeDef gpio = {0};

  if (hhcd->Instance != USB_OTG_FS)
  {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_OTG1_FS;
  HAL_GPIO_Init(GPIOA, &gpio);

  __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
#ifdef CUBE_ZEPHYR_CROSSOVER
  IRQ_CONNECT(OTG_FS_IRQn, 5, cube_usb_isr, NULL, 0);
  irq_enable(OTG_FS_IRQn);
#else
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
#endif
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef *hhcd)
{
  if (hhcd->Instance != USB_OTG_FS)
  {
    return;
  }

#ifdef CUBE_ZEPHYR_CROSSOVER
  irq_disable(OTG_FS_IRQn);
#else
  HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
#endif
  __HAL_RCC_USB2_OTG_FS_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
}

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
#ifndef CUBE_ZEPHYR_CROSSOVER
  RCC_OscInitTypeDef osc = {0};
  RCC_PeriphCLKInitTypeDef periph = {0};

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State = RCC_HSI48_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return USBH_FAIL;
  }

  periph.PeriphClockSelection = RCC_PERIPHCLK_USB;
  periph.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
  {
    return USBH_FAIL;
  }
#else
  LL_RCC_HSI48_Enable();
  uint32_t start = k_uptime_get_32();
  while (!LL_RCC_HSI48_IsReady())
  {
    if (k_uptime_get_32() - start > 100U)
    {
      printf("Cube crossover: HSI48 timeout\n");
      return USBH_FAIL;
    }
  }
  LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);
#endif

  HAL_PWREx_EnableUSBVoltageDetector();
#ifdef CUBE_ZEPHYR_CROSSOVER
  start = k_uptime_get_32();
#endif
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY))
  {
#ifdef CUBE_ZEPHYR_CROSSOVER
    if (k_uptime_get_32() - start > 100U)
    {
      printf("Cube crossover: USB33RDY timeout\n");
      return USBH_FAIL;
    }
#endif
  }

#ifdef CUBE_ZEPHYR_CROSSOVER
  (void)memset(&hhcd_usb_fs, 0, sizeof(hhcd_usb_fs));
#else
  app_memset(&hhcd_usb_fs, 0, sizeof(hhcd_usb_fs));
#endif
  hhcd_usb_fs.Instance = USB_OTG_FS;
  hhcd_usb_fs.Init.Host_channels = 8U;
  hhcd_usb_fs.Init.speed = HCD_SPEED_FULL;
  hhcd_usb_fs.Init.dma_enable = DISABLE;
  hhcd_usb_fs.Init.phy_itface = HCD_PHY_EMBEDDED;
  hhcd_usb_fs.Init.Sof_enable = ENABLE;
  hhcd_usb_fs.Init.low_power_enable = DISABLE;
  hhcd_usb_fs.Init.vbus_sensing_enable = DISABLE;
  hhcd_usb_fs.Init.use_external_vbus = DISABLE;
  hhcd_usb_fs.pData = phost;
  phost->pData = &hhcd_usb_fs;

#ifdef CUBE_ZEPHYR_CROSSOVER
  hhcd_usb_fs.State = HAL_HCD_STATE_RESET;
  HAL_HCD_MspInit(&hhcd_usb_fs);
  HAL_StatusTypeDef core = USB_CoreInit(hhcd_usb_fs.Instance, hhcd_usb_fs.Init);
  if (core != HAL_OK)
  {
    printf("Cube crossover: USB_CoreInit failed: %u\n", core);
    return USBH_FAIL;
  }
  HAL_StatusTypeDef mode = USB_SetCurrentMode(hhcd_usb_fs.Instance, USB_HOST_MODE);
  if (mode != HAL_OK)
  {
    printf("Cube crossover: USB_SetCurrentMode timed out: %u CMOD=%lu\n",
           mode, (unsigned long)USB_GetMode(hhcd_usb_fs.Instance));
  }
  HAL_StatusTypeDef host = USB_HostInit(hhcd_usb_fs.Instance, hhcd_usb_fs.Init);
  if (host != HAL_OK)
  {
    printf("Cube crossover: USB_HostInit failed: %u\n", host);
    return USBH_FAIL;
  }
  printf("Cube crossover: host ready GINTMSK=%08lx\n",
         (unsigned long)USB_OTG_FS->GINTMSK);
  hhcd_usb_fs.State = HAL_HCD_STATE_READY;
  return USBH_OK;
#else
  HAL_StatusTypeDef result = HAL_HCD_Init(&hhcd_usb_fs);
  return (result == HAL_OK) ? USBH_OK : USBH_FAIL;
#endif
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
  return (HAL_HCD_DeInit((HCD_HandleTypeDef *)phost->pData) == HAL_OK) ?
         USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
  HAL_StatusTypeDef result = HAL_HCD_Start((HCD_HandleTypeDef *)phost->pData);
#ifdef CUBE_ZEPHYR_CROSSOVER
  uint32_t start = k_uptime_get_32();
  while (result == HAL_OK && USB_GetMode(USB_OTG_FS) != USB_HOST_MODE &&
         k_uptime_get_32() - start < 100U)
  {
    k_msleep(1);
  }
  printf("Cube crossover: HCD start=%u CMOD=%lu GINTMSK=%08lx\n", result,
         (unsigned long)USB_GetMode(USB_OTG_FS),
         (unsigned long)USB_OTG_FS->GINTMSK);
  if (USB_GetMode(USB_OTG_FS) != USB_HOST_MODE)
  {
    return USBH_FAIL;
  }
#endif
  return (result == HAL_OK) ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
  return (HAL_HCD_Stop((HCD_HandleTypeDef *)phost->pData) == HAL_OK) ?
         USBH_OK : USBH_FAIL;
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
  switch (HAL_HCD_GetCurrentSpeed((HCD_HandleTypeDef *)phost->pData))
  {
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
  return (HAL_HCD_ResetPort((HCD_HandleTypeDef *)phost->pData) == HAL_OK) ?
         USBH_OK : USBH_FAIL;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
  return HAL_HCD_HC_GetXferCount((HCD_HandleTypeDef *)phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe,
                                    uint8_t epnum, uint8_t dev_address,
                                    uint8_t speed, uint8_t ep_type,
                                    uint16_t mps)
{
  return (HAL_HCD_HC_Init((HCD_HandleTypeDef *)phost->pData, pipe, epnum,
                          dev_address, speed, ep_type, mps) == HAL_OK) ?
         USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
  return (HAL_HCD_HC_Halt((HCD_HandleTypeDef *)phost->pData, pipe) == HAL_OK) ?
         USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
  (void)phost;
  (void)pipe;
  return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe,
                                     uint8_t direction, uint8_t ep_type,
                                     uint8_t token, uint8_t *buffer,
                                     uint16_t length, uint8_t do_ping)
{
  return (HAL_HCD_HC_SubmitRequest((HCD_HandleTypeDef *)phost->pData, pipe,
                                   direction, ep_type, token, buffer, length,
                                   do_ping) == HAL_OK) ? USBH_OK : USBH_FAIL;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
  return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(
      (HCD_HandleTypeDef *)phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
  (void)phost;
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10,
                    (state != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_Delay(100U);
  printf("VBUS %s: PD10=%u PA9=%u\r\n",
         (state != 0U) ? "on" : "off",
         (unsigned int)HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_10),
         (unsigned int)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9));
  return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe,
                                     uint8_t toggle)
{
  HCD_HandleTypeDef *hhcd = (HCD_HandleTypeDef *)phost->pData;

  if (hhcd->hc[pipe].ep_is_in != 0U)
  {
    hhcd->hc[pipe].toggle_in = toggle;
  }
  else
  {
    hhcd->hc[pipe].toggle_out = toggle;
  }
  return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
  HCD_HandleTypeDef *hhcd = (HCD_HandleTypeDef *)phost->pData;

  return (hhcd->hc[pipe].ep_is_in != 0U) ?
         hhcd->hc[pipe].toggle_in : hhcd->hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t delay)
{
  HAL_Delay(delay);
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
  (void)USBH_LL_Connect((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
  (void)USBH_LL_Disconnect((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
  USBH_LL_PortEnabled((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
  USBH_LL_PortDisabled((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
  USBH_LL_IncTimer((USBH_HandleTypeDef *)hhcd->pData);
}
