/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_HELLO_SHELL_BOARD_TEMPERATURE_H_
#define ZEPHYR_HELLO_SHELL_BOARD_TEMPERATURE_H_

#include <stdint.h>

struct board_temperature {
	int32_t milli_celsius;
	const char *source;
};

int board_temperature_get(struct board_temperature *temperature);

#endif /* ZEPHYR_HELLO_SHELL_BOARD_TEMPERATURE_H_ */
