/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_temperature.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#define DIE_TEMP_NODE DT_NODELABEL(die_temp)
#define DIE_TEMP_REFRESH_MS 1000

/* The dashboard polls /api/v1/io every 100 ms; sample the ADC at most once per second. */
int board_temperature_get(struct board_temperature *temperature)
{
	if (temperature == NULL) {
		return -EINVAL;
	}

#if DT_NODE_HAS_STATUS_OKAY(DIE_TEMP_NODE)
	static const struct device *const dev = DEVICE_DT_GET(DIE_TEMP_NODE);
	static K_MUTEX_DEFINE(lock);
	static int64_t last_ms;
	static int32_t cached_milli;
	static int cached_ret = -EAGAIN;
	struct sensor_value val;
	int64_t now = k_uptime_get();
	int ret;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (cached_ret != 0 || (now - last_ms) >= DIE_TEMP_REFRESH_MS) {
		ret = sensor_sample_fetch(dev);
		if (ret == 0) {
			ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &val);
		}
		if (ret == 0) {
			cached_milli = (int32_t)sensor_value_to_milli(&val);
		}
		cached_ret = ret;
		last_ms = now;
	}
	ret = cached_ret;
	if (ret == 0) {
		temperature->milli_celsius = cached_milli;
		temperature->source = "mcu_die";
	}
	k_mutex_unlock(&lock);

	return ret;
#else
	ARG_UNUSED(temperature);
	return -ENOTSUP;
#endif
}
