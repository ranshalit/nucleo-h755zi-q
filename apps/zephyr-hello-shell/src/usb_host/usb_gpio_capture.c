/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_gpio_capture.h"

#include <string.h>

#include <zephyr/irq.h>

#include <stm32h7xx.h>

/* PA11 = OTG_FS_DM (D-), PA12 = OTG_FS_DP (D+). */
#define GPIOA_IDR_DM_BIT (1U << 11)
#define GPIOA_IDR_DP_BIT (1U << 12)

static uint32_t capture_buf[USB_GPIO_CAPTURE_MAX_SAMPLES];
static uint32_t capture_count;
static uint32_t capture_cycles;
static volatile int capture_armed;

void usb_gpio_capture_arm(void)
{
	capture_armed = 1;
}

int usb_gpio_capture_armed(void)
{
	return capture_armed;
}

void usb_gpio_capture_disarm(void)
{
	capture_armed = 0;
}

static void dwt_enable(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void usb_gpio_capture_run(uint32_t num_samples)
{
	uint32_t n = num_samples;
	uint32_t start;
	uint32_t end;
	unsigned int key;

	if (n == 0U || n > USB_GPIO_CAPTURE_MAX_SAMPLES) {
		n = USB_GPIO_CAPTURE_MAX_SAMPLES;
	}

	dwt_enable();

	/*
	 * Interrupts are disabled for the capture window so the loop timing
	 * is deterministic. This only affects USB *interrupt processing*
	 * (e.g. channel-halted completion), not the electrical transaction
	 * itself, which is handled autonomously by the OTG_FS hardware state
	 * machine. Keep the window short (a few hundred microseconds to low
	 * single-digit milliseconds) to avoid perturbing retry timing.
	 */
	key = irq_lock();
	start = DWT->CYCCNT;
	for (uint32_t i = 0; i < n; i++) {
		capture_buf[i] = GPIOA->IDR;
	}
	end = DWT->CYCCNT;
	irq_unlock(key);

	capture_count = n;
	capture_cycles = end - start;
}

void usb_gpio_capture_get(const uint32_t **out_buf, uint32_t *out_count,
			   uint32_t *out_cycles)
{
	if (out_buf != NULL) {
		*out_buf = capture_buf;
	}
	if (out_count != NULL) {
		*out_count = capture_count;
	}
	if (out_cycles != NULL) {
		*out_cycles = capture_cycles;
	}
}
