/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_temperature.h"

#include <errno.h>
#include <stddef.h>

int board_temperature_get(struct board_temperature *temperature)
{
	if (temperature == NULL) {
		return -EINVAL;
	}

	temperature->milli_celsius = 25000;
	temperature->source = "simulated";
	return 0;
}
