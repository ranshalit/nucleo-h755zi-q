/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_io.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

struct board_led {
	struct board_led_info info;
	bool is_on;
};

static struct board_led board_leds[] = {
	{
		.info = {
			.color = "green",
			.label = "LD1",
			.blinking = false,
			.blink_period_ms = 0,
		},
	},
	{
		.info = {
			.color = "yellow",
			.label = "LD2",
			.blinking = true,
			.blink_period_ms = 400U,
		},
	},
	{
		.info = {
			.color = "red",
			.label = "LD3",
			.blinking = true,
			.blink_period_ms = 700U,
		},
	},
};

static bool user_button_pressed;
static bool board_io_initialized;
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
	uint32_t blink_period_ms = led->info.blink_period_ms;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_mutex_lock(&board_io_lock, K_FOREVER);
		led->is_on = !led->is_on;
		k_mutex_unlock(&board_io_lock);

		k_msleep(blink_period_ms);
	}
}

int board_io_init(void)
{
	if (board_io_initialized) {
		return 0;
	}

	board_io_initialized = true;
	return 0;
}

int board_blink_threads_start(void)
{
	int ret = board_io_init();

	if (ret != 0) {
		return ret;
	}

	if (blink_threads_started) {
		return 0;
	}

	k_tid_t yellow_tid = k_thread_create(&yellow_led_blink_thread, yellow_led_blink_stack,
					     K_THREAD_STACK_SIZEOF(yellow_led_blink_stack),
					     led_blink_thread, &board_leds[BOARD_LED_YELLOW],
					     NULL, NULL, LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_tid_t red_tid = k_thread_create(&red_led_blink_thread, red_led_blink_stack,
					  K_THREAD_STACK_SIZEOF(red_led_blink_stack),
					  led_blink_thread, &board_leds[BOARD_LED_RED], NULL, NULL,
					  LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(yellow_tid, "yellow_led_blink");
	k_thread_name_set(red_tid, "red_led_blink");
	blink_threads_started = true;

	return 0;
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

size_t board_led_count(void)
{
	return ARRAY_SIZE(board_leds);
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

	if (led == NULL || is_on == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);
	*is_on = led->is_on;
	k_mutex_unlock(&board_io_lock);

	return 0;
}

int board_led_apply_action(enum board_led_id led_id, const char *action, bool *is_on)
{
	struct board_led *led = board_led_from_id(led_id);
	int ret = 0;

	if (led == NULL || action == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);

	if (strcmp(action, "on") == 0) {
		led->is_on = true;
	} else if (strcmp(action, "off") == 0) {
		led->is_on = false;
	} else if (strcmp(action, "toggle") == 0) {
		led->is_on = !led->is_on;
	} else {
		ret = -EINVAL;
	}

	if (ret == 0 && is_on != NULL) {
		*is_on = led->is_on;
	}

	k_mutex_unlock(&board_io_lock);
	return ret;
}

const char *board_button_label(void)
{
	return "B1 (simulated)";
}

int board_button_get(bool *pressed)
{
	if (pressed == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&board_io_lock, K_FOREVER);
	*pressed = user_button_pressed;
	k_mutex_unlock(&board_io_lock);

	return 0;
}

void board_io_native_sim_button_set(bool pressed)
{
	k_mutex_lock(&board_io_lock, K_FOREVER);
	user_button_pressed = pressed;
	k_mutex_unlock(&board_io_lock);
}
