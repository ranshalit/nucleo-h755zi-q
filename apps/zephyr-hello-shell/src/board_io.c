/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_io.h"

#include <errno.h>
#include <string.h>

#include <zephyr/app_memory/mem_domain.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define LED_USER_REGION_SIZE 64U

struct board_led {
	enum board_led_id id;
	struct board_led_info info;
	struct gpio_dt_spec gpio;
};

struct board_led_region {
	struct board_led led;
	uint8_t padding[LED_USER_REGION_SIZE - sizeof(struct board_led)];
} __aligned(LED_USER_REGION_SIZE);

BUILD_ASSERT(sizeof(struct board_led) <= LED_USER_REGION_SIZE);
BUILD_ASSERT(sizeof(struct board_led_region) == LED_USER_REGION_SIZE);

static struct board_led green_led = {
	.id = BOARD_LED_GREEN,
	.info =
		{
			.color = "green",
			.label = "LD1",
			.blinking = false,
			.blink_period_ms = 0,
		},
	.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios),
};

static struct board_led_region yellow_led_region = {
	.led =
		{
			.id = BOARD_LED_YELLOW,
			.info =
				{
					.color = "yellow",
					.label = "LD2",
					.blinking = true,
					.blink_period_ms = 400U,
				},
			.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(yellow_led), gpios),
		},
};

static struct board_led_region red_led_region = {
	.led =
		{
			.id = BOARD_LED_RED,
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

K_MEM_PARTITION_DEFINE(yellow_led_partition, &yellow_led_region, sizeof(yellow_led_region),
		       K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(red_led_partition, &red_led_region, sizeof(red_led_region),
		       K_MEM_PARTITION_P_RW_U_RW);

static struct k_mem_domain yellow_led_domain;
static struct k_mem_domain red_led_domain;

static struct k_mem_partition *yellow_led_partitions[] = {
	&yellow_led_partition,
};
static struct k_mem_partition *red_led_partitions[] = {
	&red_led_partition,
};

static struct board_led *const board_leds[] = {
	&green_led,
	&yellow_led_region.led,
	&red_led_region.led,
};

static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

static bool board_io_initialized;
static int board_io_init_status;
static bool blink_threads_started;
static int blink_threads_start_error;

static K_MUTEX_DEFINE(board_io_lock);

#define LED_BLINK_THREAD_STACK_SIZE 512
#define LED_BLINK_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(yellow_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(red_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);

static struct k_thread yellow_led_blink_thread;
static struct k_thread red_led_blink_thread;

#if defined(CONFIG_APP_MPU_PROTECTION_TEST)
#define MPU_PROBE_THREAD_STACK_SIZE 256
K_THREAD_STACK_DEFINE(mpu_probe_stack, MPU_PROBE_THREAD_STACK_SIZE);
static struct k_thread mpu_probe_thread_data;

static void mpu_probe_thread(void *arg1, void *arg2, void *arg3)
{
	volatile uint8_t *address = arg1;
	int write_access = POINTER_TO_INT(arg2);

	if (write_access != 0) {
		*address = 0xA5U;
	} else {
		volatile uint8_t value = *address;
		ARG_UNUSED(value);
	}
	ARG_UNUSED(arg3);
}

static int cmd_mpu_probe(const struct shell *shell, size_t argc, char **argv)
{
	volatile uint8_t *address;
	bool write_access;
	k_tid_t tid;
	int ret;

	if (argc != 2) {
		shell_error(shell,
			    "usage: mpu_probe state-read|state-write|stack-read|stack-write");
		return -EINVAL;
	}

	if ((strcmp(argv[1], "state-read") == 0) || (strcmp(argv[1], "state-write") == 0)) {
		address = (volatile uint8_t *)&red_led_region.led.info.blink_period_ms;
	} else if ((strcmp(argv[1], "stack-read") == 0) ||
		   (strcmp(argv[1], "stack-write") == 0)) {
		address = (volatile uint8_t *)K_THREAD_STACK_BUFFER(red_led_blink_stack);
	} else {
		shell_error(shell, "unknown probe mode");
		return -EINVAL;
	}

	write_access = (strcmp(argv[1], "state-write") == 0) ||
		       (strcmp(argv[1], "stack-write") == 0);

	tid = k_thread_create(&mpu_probe_thread_data, mpu_probe_stack,
			      K_THREAD_STACK_SIZEOF(mpu_probe_stack), mpu_probe_thread,
			      (void *)address, INT_TO_POINTER(write_access), NULL,
			      LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);
	ret = k_mem_domain_add_thread(&yellow_led_domain, tid);
	if (ret != 0) {
		k_thread_abort(tid);
		shell_error(shell, "could not assign probe memory domain: %d", ret);
		return ret;
	}

	k_thread_name_set(tid, "mpu_probe");
	shell_print(shell, "started %s; expected MPU fault", argv[1]);
	k_thread_start(tid);
	return 0;
}

SHELL_CMD_REGISTER(mpu_probe, NULL, "Test LED MPU isolation", cmd_mpu_probe);
#endif

struct board_led_error {
	enum board_led_id led_id;
	int error;
};

K_MSGQ_DEFINE(board_io_error_queue, sizeof(struct board_led_error), 4, 4);
K_THREAD_STACK_DEFINE(board_io_error_stack, 1024);
static struct k_thread board_io_error_thread;

static void board_io_error_thread_entry(void *arg1, void *arg2, void *arg3)
{
	struct board_led_error error;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		ret = k_msgq_get(&board_io_error_queue, &error, K_FOREVER);
		if (ret != 0) {
			printk("LED error queue receive failed: %d\n", ret);
			continue;
		}

		if (error.led_id < BOARD_LED_GREEN || error.led_id >= BOARD_LED_COUNT) {
			printk("LED error queue received invalid LED ID: %d\n", error.led_id);
			continue;
		}

		printk("%s LED toggle failed: %d\n", board_leds[error.led_id]->info.color, error.error);
	}
}

static struct board_led *board_led_from_id(enum board_led_id led_id)
{
	if (led_id < BOARD_LED_GREEN || led_id >= BOARD_LED_COUNT) {
		return NULL;
	}

	return board_leds[led_id];
}

static void led_blink_thread(void *arg1, void *arg2, void *arg3)
{
	struct board_led *led = arg1;
	struct board_led_error error;
	const int blink_period_ms = POINTER_TO_INT(arg2);
	int ret;

	ARG_UNUSED(arg3);

	while (true) {
		k_mutex_lock(&board_io_lock, K_FOREVER);
		ret = gpio_pin_toggle_dt(&led->gpio);
		k_mutex_unlock(&board_io_lock);

		if (ret != 0) {
			error.led_id = led->id;
			error.error = ret;
			ret = k_msgq_put(&board_io_error_queue, &error, K_FOREVER);
			if (ret != 0) {
				k_oops();
			}
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
		if (!gpio_is_ready_dt(&board_leds[i]->gpio)) {
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
		ret = gpio_pin_configure_dt(&board_leds[i]->gpio, GPIO_OUTPUT_INACTIVE);
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

	if (blink_threads_start_error != 0) {
		return blink_threads_start_error;
	}

	ret = k_mem_domain_init(&yellow_led_domain, ARRAY_SIZE(yellow_led_partitions),
				yellow_led_partitions);
	if (ret != 0) {
		blink_threads_start_error = ret;
		return ret;
	}

	ret = k_mem_domain_init(&red_led_domain, ARRAY_SIZE(red_led_partitions), red_led_partitions);
	if (ret != 0) {
		blink_threads_start_error = ret;
		return ret;
	}

	k_tid_t yellow_tid = k_thread_create(&yellow_led_blink_thread, yellow_led_blink_stack,
					     K_THREAD_STACK_SIZEOF(yellow_led_blink_stack),
					     led_blink_thread, &yellow_led_region.led,
					     INT_TO_POINTER(
						     yellow_led_region.led.info.blink_period_ms),
					     NULL, LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);
	k_tid_t red_tid = k_thread_create(&red_led_blink_thread, red_led_blink_stack,
					  K_THREAD_STACK_SIZEOF(red_led_blink_stack),
					  led_blink_thread, &red_led_region.led,
					  INT_TO_POINTER(red_led_region.led.info.blink_period_ms),
					  NULL, LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);

	ret = k_mem_domain_add_thread(&yellow_led_domain, yellow_tid);
	if (ret == 0) {
		ret = k_mem_domain_add_thread(&red_led_domain, red_tid);
	}
	if (ret != 0) {
		k_thread_abort(yellow_tid);
		k_thread_abort(red_tid);
		blink_threads_start_error = ret;
		return ret;
	}

	k_object_access_grant(&board_io_lock, yellow_tid);
	k_object_access_grant(yellow_led_region.led.gpio.port, yellow_tid);
	k_object_access_grant(&board_io_error_queue, yellow_tid);
	k_object_access_grant(&board_io_lock, red_tid);
	k_object_access_grant(red_led_region.led.gpio.port, red_tid);
	k_object_access_grant(&board_io_error_queue, red_tid);

	/* Created only after all fallible setup, so no failure path can leave it running. */
	k_tid_t logger_tid =
		k_thread_create(&board_io_error_thread, board_io_error_stack,
				K_THREAD_STACK_SIZEOF(board_io_error_stack),
				board_io_error_thread_entry, NULL, NULL, NULL,
				LED_BLINK_THREAD_PRIORITY + 1, 0, K_NO_WAIT);

	k_thread_name_set(logger_tid, "board_io_error");
	k_thread_name_set(yellow_tid, "yellow_led_blink");
	k_thread_name_set(red_tid, "red_led_blink");
	k_thread_start(yellow_tid);
	k_thread_start(red_tid);
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
		if (strcmp(board_leds[i]->info.color, color) == 0) {
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
