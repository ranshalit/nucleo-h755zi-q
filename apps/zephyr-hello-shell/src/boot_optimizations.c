/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/version.h>

#include <stm32h7xx.h>
#include <stm32h7xx_ll_bus.h>
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_ll_usart.h>

static void board_early_uart_init(void)
{
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOD);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);

	LL_GPIO_SetPinMode(GPIOD, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinOutputType(GPIOD, LL_GPIO_PIN_8, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinSpeed(GPIOD, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_VERY_HIGH);
	LL_GPIO_SetPinPull(GPIOD, LL_GPIO_PIN_8, LL_GPIO_PULL_NO);
	LL_GPIO_SetAFPin_8_15(GPIOD, LL_GPIO_PIN_8, LL_GPIO_AF_7);

	LL_USART_Disable(USART3);
	LL_USART_ConfigAsyncMode(USART3);
	LL_USART_SetTransferDirection(USART3, LL_USART_DIRECTION_TX);
	LL_USART_SetBaudRate(USART3, HSI_VALUE, LL_USART_PRESCALER_DIV1,
			     LL_USART_OVERSAMPLING_16, 115200U);
	LL_USART_Enable(USART3);

	while (!LL_USART_IsActiveFlag_TEACK(USART3)) {
	}
}

static void board_early_usb_vbus_off(void)
{
	LL_GPIO_SetPinMode(GPIOD, LL_GPIO_PIN_10, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(GPIOD, LL_GPIO_PIN_10, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinSpeed(GPIOD, LL_GPIO_PIN_10, LL_GPIO_SPEED_FREQ_LOW);
	LL_GPIO_SetPinPull(GPIOD, LL_GPIO_PIN_10, LL_GPIO_PULL_NO);
	/* MB1363 U18 (STMPS2141STR): PD10 low enables VBUS, high disables it. */
	LL_GPIO_SetOutputPin(GPIOD, LL_GPIO_PIN_10);
}

static void board_early_uart_putc(char c)
{
	while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART3)) {
	}

	USART3->TDR = (uint8_t)c;
}

static void board_early_uart_puts(const char *str)
{
	while (*str != '\0') {
		if (*str == '\n') {
			board_early_uart_putc('\r');
		}

		board_early_uart_putc(*str++);
	}

	while (!LL_USART_IsActiveFlag_TC(USART3)) {
	}
}

static void board_emit_boot_banner(void)
{
	board_early_uart_puts("*** Booting Zephyr OS build ***\n");
	board_early_uart_puts("*** Booting Zephyr OS build ***\n");
	LL_USART_Disable(USART3);
}

#ifdef CONFIG_SOC_PREP_HOOK
void soc_prep_hook(void)
{
	board_early_uart_init();
	board_early_usb_vbus_off();
	board_emit_boot_banner();
}
#else
static int board_early_boot_banner(void)
{
	board_early_uart_init();
	board_early_usb_vbus_off();
	board_emit_boot_banner();

	return 0;
}

SYS_INIT(board_early_boot_banner, EARLY, 0);
#endif
