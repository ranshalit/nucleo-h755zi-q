/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_io.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

struct board_led {
	struct board_led_info info;
	struct gpio_dt_spec gpio;
};

static struct board_led board_leds[] = {
	{
		.info =
			{
				.color = "green",
				.label = "LD1",
				.blinking = false,
				.blink_period_ms = 0,
			},
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios),
	},
	{
		.info =
			{
				.color = "yellow",
				.label = "LD2",
				.blinking = true,
				.blink_period_ms = 400U,
			},
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(yellow_led), gpios),
	},
	{
		.info =
			{
				.color = "red",
				.label = "LD3",
				.blinking = true,
				.blink_period_ms = 700U,
			},
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios),
	},
};

static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

static bool board_io_initialized;
static int board_io_init_status;
static bool blink_threads_started;

static K_MUTEX_DEFINE(board_io_lock);

#define LED_BLINK_THREAD_STACK_SIZE 512
#define LED_BLINK_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(yellow_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(red_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);

static struct k_thread yellow_led_blink_thread;
static struct k_thread red_led_blink_thread;

static struct board_led *board_led_from_id(enum board_led_id led_id)
{
	if (led_id < BOARD_LED_GREEN || led_id >= BOARD_LED_COUNT) {
		return NULL;
	}

	return &board_leds[led_id];
}

static void led_blink_thread(void *arg1, void *arg2, void *arg3)
{
	struct board_led *led = arg1;
	const int blink_period_ms = POINTER_TO_INT(arg2);
	int ret;

	ARG_UNUSED(arg3);

	while (true) {
		k_mutex_lock(&board_io_lock, K_FOREVER);
		ret = gpio_pin_toggle_dt(&led->gpio);
		k_mutex_unlock(&board_io_lock);

		if (ret != 0) {
			printk("%s LED toggle failed: %d\n", led->info.color, ret);
		}

		k_msleep(blink_period_ms);
	}
}

int board_io_init(void)
{
	int ret;

	if (board_io_initialized) {
		return board_io_init_status;
	}

	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		if (!gpio_is_ready_dt(&board_leds[i].gpio)) {
			board_io_initialized = true;
			board_io_init_status = -ENODEV;
			return board_io_init_status;
		}
	}

	if (!gpio_is_ready_dt(&user_button)) {
		board_io_initialized = true;
		board_io_init_status = -ENODEV;
		return board_io_init_status;
	}

	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		ret = gpio_pin_configure_dt(&board_leds[i].gpio, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			board_io_initialized = true;
			board_io_init_status = ret;
			return ret;
		}
	}

	ret = gpio_pin_configure_dt(&user_button, GPIO_INPUT);
	if (ret != 0) {
		board_io_initialized = true;
		board_io_init_status = ret;
		return ret;
	}

	board_io_initialized = true;
	board_io_init_status = 0;
	return 0;
}

int board_blink_threads_start(void)
{
	int ret;

	ret = board_io_init();
	if (ret != 0) {
		return ret;
	}

	if (blink_threads_started) {
		return 0;
	}

	k_tid_t yellow_tid = k_thread_create(&yellow_led_blink_thread, yellow_led_blink_stack,
					     K_THREAD_STACK_SIZEOF(yellow_led_blink_stack),
					     led_blink_thread, (void *)&board_leds[BOARD_LED_YELLOW],
					     INT_TO_POINTER(board_leds[BOARD_LED_YELLOW]
								.info.blink_period_ms),
					     NULL, LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_tid_t red_tid = k_thread_create(&red_led_blink_thread, red_led_blink_stack,
					  K_THREAD_STACK_SIZEOF(red_led_blink_stack),
					  led_blink_thread, (void *)&board_leds[BOARD_LED_RED],
					  INT_TO_POINTER(board_leds[BOARD_LED_RED]
							     .info.blink_period_ms),
					  NULL, LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(yellow_tid, "yellow_led_blink");
	k_thread_name_set(red_tid, "red_led_blink");
	blink_threads_started = true;

	return 0;
}

size_t board_led_count(void)
{
	return ARRAY_SIZE(board_leds);
}

void board_blink_threads_suspend(void)
{
	if (blink_threads_started) {
		k_thread_suspend(&yellow_led_blink_thread);
		k_thread_suspend(&red_led_blink_thread);
	}
}

void board_blink_threads_resume(void)
{
	if (blink_threads_started) {
		k_thread_resume(&yellow_led_blink_thread);
		k_thread_resume(&red_led_blink_thread);
	}
}

const struct board_led_info *board_led_info_get(enum board_led_id led_id)
{
	struct board_led *led = board_led_from_id(led_id);

	return led == NULL ? NULL : &led->info;
}

int board_led_find(const char *color, enum board_led_id *led_id)
{
	if (color == NULL || led_id == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		if (strcmp(board_leds[i].info.color, color) == 0) {
			*led_id = (enum board_led_id)i;
			return 0;
		}
	}

	return -ENOENT;
}

int board_led_get(enum board_led_id led_id, bool *is_on)
{
	struct board_led *led = board_led_from_id(led_id);
	int value;

	if (led == NULL || is_on == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);
	value = gpio_pin_get_dt(&led->gpio);
	k_mutex_unlock(&board_io_lock);

	if (value < 0) {
		return value;
	}

	*is_on = (value != 0);
	return 0;
}

int board_led_apply_action(enum board_led_id led_id, const char *action, bool *is_on)
{
	struct board_led *led = board_led_from_id(led_id);
	int ret;
	int value;

	if (led == NULL || action == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);

	if (strcmp(action, "on") == 0) {
		ret = gpio_pin_set_dt(&led->gpio, 1);
	} else if (strcmp(action, "off") == 0) {
		ret = gpio_pin_set_dt(&led->gpio, 0);
	} else if (strcmp(action, "toggle") == 0) {
		ret = gpio_pin_toggle_dt(&led->gpio);
	} else {
		k_mutex_unlock(&board_io_lock);
		return -EINVAL;
	}

	if (ret == 0 && is_on != NULL) {
		value = gpio_pin_get_dt(&led->gpio);
		if (value < 0) {
			ret = value;
		} else {
			*is_on = (value != 0);
		}
	}

	k_mutex_unlock(&board_io_lock);
	return ret;
}

const char *board_button_label(void)
{
	return "B1";
}

int board_button_get(bool *pressed)
{
	int value;

	if (pressed == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);
	value = gpio_pin_get_dt(&user_button);
	k_mutex_unlock(&board_io_lock);

	if (value < 0) {
		return value;
	}

	*pressed = (value != 0);
	return 0;
}
