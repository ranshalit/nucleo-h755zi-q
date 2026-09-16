/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_USB_GPIO_CAPTURE_H_
#define APP_USB_GPIO_CAPTURE_H_

#include <stdint.h>

/**
 * @brief Software "poor man's logic analyzer" for the USER USB D+/D- lines.
 *
 * Captures GPIOA->IDR as fast as the CPU can loop, with interrupts disabled
 * for determinism, timestamping the run with the Cortex-M7 DWT cycle
 * counter. This does not use any DMA/timer peripheral, so it cannot
 * interfere with the already-working USB HCD peripheral configuration.
 *
 * Full-speed USB is 12 Mbit/s NRZI + bit-stuffed and would need ~48 MHz
 * oversampling for a bit-accurate decode; this tight-loop approach reaches
 * a much lower, CPU-bound sample rate, so it can only reveal coarse
 * line-state (J/K/SE0) activity/timing, not a guaranteed clean protocol
 * decode. Still useful to answer "is anything electrically happening on
 * D+/D- at all" during a control-transfer attempt.
 */

/** Maximum samples the static capture buffer can hold. */
#define USB_GPIO_CAPTURE_MAX_SAMPLES 32768

/**
 * @brief Run a tight-loop GPIOA->IDR capture.
 *
 * @param num_samples number of samples to capture (clamped to
 *        USB_GPIO_CAPTURE_MAX_SAMPLES).
 */
void usb_gpio_capture_run(uint32_t num_samples);

/**
 * @brief Access the most recent capture buffer/metadata.
 *
 * @param out_buf receives a pointer to the internal sample buffer.
 * @param out_count receives the number of valid samples captured.
 * @param out_cycles receives the total DWT cycle count elapsed across the
 *        capture (0 if DWT is unavailable/not started).
 */
void usb_gpio_capture_get(const uint32_t **out_buf, uint32_t *out_count,
			   uint32_t *out_cycles);

/**
 * @brief Arm a one-shot capture that starts synchronously on the next
 * SETUP-token URB submission (see usb_host/stm32_hcd_adapter.c). Clears
 * itself automatically once the triggered capture completes.
 */
void usb_gpio_capture_arm(void);

/** @brief Query whether a capture is currently armed. */
int usb_gpio_capture_armed(void);

/** @brief Cancel/clear the armed flag without a capture happening. */
void usb_gpio_capture_disarm(void);

#endif /* APP_USB_GPIO_CAPTURE_H_ */
