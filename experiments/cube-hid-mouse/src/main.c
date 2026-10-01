#include "app.h"
#include "usbh_core.h"
#include "usbh_hid.h"

#include <stdint.h>
#include <stdio.h>

UART_HandleTypeDef huart3;
static USBH_HandleTypeDef hUsbHostFS;
static volatile uint32_t mouse_report_count;

static void MPU_Config(void);
static void CPU_CACHE_Enable(void);
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

int main(void)
{
  MPU_Config();
  CPU_CACHE_Enable();

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART3_UART_Init();

  printf("\r\nNUCLEO-H755ZI-Q Cube USB HID mouse host\r\n");
  printf("CM7 @ %lu Hz, USB FS PA11/PA12, HSI48, PD10 active-low VBUS\r\n",
         (unsigned long)HAL_RCC_GetSysClockFreq());
  printf("Expected test mouse: Dell/PixArt 413c:301a\r\n");

  if (USBH_Init(&hUsbHostFS, USBH_UserProcess, 0U) != USBH_OK)
  {
    printf("USBH_Init failed\r\n");
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostFS, USBH_HID_CLASS) != USBH_OK)
  {
    printf("USBH_RegisterClass(HID) failed\r\n");
    Error_Handler();
  }
  if (USBH_Start(&hUsbHostFS) != USBH_OK)
  {
    printf("USBH_Start failed\r\n");
    Error_Handler();
  }

  printf("USB host started; waiting for device\r\n");
  while (1)
  {
    (void)USBH_Process(&hUsbHostFS);
  }
}

static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
  switch (id)
  {
    case HOST_USER_CONNECTION:
      printf("USB connected\r\n");
      break;
    case HOST_USER_SELECT_CONFIGURATION:
      printf("USB configuration selected\r\n");
      break;
    case HOST_USER_CLASS_SELECTED:
      printf("USB HID class selected\r\n");
      break;
    case HOST_USER_CLASS_ACTIVE:
      printf("USB HID active: VID=%04x PID=%04x type=%u\r\n",
             phost->device.DevDesc.idVendor, phost->device.DevDesc.idProduct,
             (unsigned int)USBH_HID_GetDeviceType(phost));
      if ((phost->device.DevDesc.idVendor == 0x413CU) &&
          (phost->device.DevDesc.idProduct == 0x301AU))
      {
        printf("Matched Dell MS116 / PixArt 413c:301a\r\n");
      }
      break;
    case HOST_USER_DISCONNECTION:
      printf("USB disconnected\r\n");
      break;
    case HOST_USER_UNRECOVERED_ERROR:
      printf("USB unrecovered error\r\n");
      break;
    default:
      break;
  }
}

void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
  HID_MOUSE_Info_TypeDef *mouse;

  if (USBH_HID_GetDeviceType(phost) != HID_MOUSE)
  {
    return;
  }

  mouse = USBH_HID_GetMouseInfo(phost);
  if (mouse == NULL)
  {
    printf("HID report decode failed\r\n");
    return;
  }

  mouse_report_count++;
  printf("HID mouse report #%lu buttons=%u%u%u dx=%d dy=%d\r\n",
         (unsigned long)mouse_report_count,
         mouse->buttons[0], mouse->buttons[1], mouse->buttons[2],
         (int)(int8_t)mouse->x, (int)(int8_t)mouse->y);
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_BYPASS;
  osc.HSIState = RCC_HSI_OFF;
  osc.CSIState = RCC_CSI_OFF;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 4U;
  osc.PLL.PLLN = 400U;
  osc.PLL.PLLP = 2U;
  osc.PLL.PLLQ = 4U;
  osc.PLL.PLLR = 2U;
  osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
  osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  osc.PLL.PLLFRACN = 0U;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
  clk.AHBCLKDivider = RCC_HCLK_DIV2;
  clk.APB3CLKDivider = RCC_APB3_DIV2;
  clk.APB1CLKDivider = RCC_APB1_DIV2;
  clk.APB2CLKDivider = RCC_APB2_DIV2;
  clk.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, GPIO_PIN_SET);
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &gpio);
}

static void MX_USART3_UART_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_USART3_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &gpio);

  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200U;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

static void CPU_CACHE_Enable(void)
{
  SCB_EnableICache();
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef region = {0};

  HAL_MPU_Disable();
  region.Enable = MPU_REGION_ENABLE;
  region.BaseAddress = 0x00000000U;
  region.Size = MPU_REGION_SIZE_4GB;
  region.AccessPermission = MPU_REGION_NO_ACCESS;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.Number = MPU_REGION_NUMBER0;
  region.TypeExtField = MPU_TEX_LEVEL0;
  region.SubRegionDisable = 0x87U;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
