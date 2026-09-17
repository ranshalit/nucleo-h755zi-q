/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_HELLO_SHELL_BOARD_IO_H_
#define ZEPHYR_HELLO_SHELL_BOARD_IO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum board_led_id {
	BOARD_LED_GREEN = 0,
	BOARD_LED_YELLOW,
	BOARD_LED_RED,
	BOARD_LED_COUNT,
};

struct board_led_info {
	const char *color;
	const char *label;
	bool blinking;
	uint32_t blink_period_ms;
};

int board_io_init(void);
int board_blink_threads_start(void);
void board_blink_threads_suspend(void);
void board_blink_threads_resume(void);

size_t board_led_count(void);
const struct board_led_info *board_led_info_get(enum board_led_id led_id);
int board_led_find(const char *color, enum board_led_id *led_id);
int board_led_get(enum board_led_id led_id, bool *is_on);
int board_led_apply_action(enum board_led_id led_id, const char *action, bool *is_on);

const char *board_button_label(void);
int board_button_get(bool *pressed);

#endif /* ZEPHYR_HELLO_SHELL_BOARD_IO_H_ */
