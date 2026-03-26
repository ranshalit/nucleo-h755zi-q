/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

struct board_led {
	const char *color;
	const char *label;
	struct gpio_dt_spec gpio;
};

enum board_led_index {
	BOARD_LED_GREEN = 0,
	BOARD_LED_YELLOW,
	BOARD_LED_RED,
};

static const struct board_led board_leds[] = {
	{
		.color = "green",
		.label = "LD1",
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios),
	},
	{
		.color = "yellow",
		.label = "LD2",
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(yellow_led), gpios),
	},
	{
		.color = "red",
		.label = "LD3",
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios),
	},
};

static bool leds_initialized;
static int leds_init_status;

#define LED_BLINK_THREAD_STACK_SIZE 512
#define LED_BLINK_THREAD_PRIORITY 7
#define YELLOW_LED_BLINK_PERIOD_MS 400
#define RED_LED_BLINK_PERIOD_MS 700

K_THREAD_STACK_DEFINE(yellow_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(red_led_blink_stack, LED_BLINK_THREAD_STACK_SIZE);

static struct k_thread yellow_led_blink_thread;
static struct k_thread red_led_blink_thread;
static bool blink_threads_started;

static void led_blink_thread(void *arg1, void *arg2, void *arg3)
{
	const struct board_led *led = arg1;
	const int blink_period_ms = POINTER_TO_INT(arg2);

	ARG_UNUSED(arg3);

	while (true) {
		int ret = gpio_pin_toggle_dt(&led->gpio);

		if (ret != 0) {
			printk("%s LED toggle failed: %d\n", led->color, ret);
		}

		k_msleep(blink_period_ms);
	}
}

static int init_leds(void)
{
	int ret;

	if (leds_initialized) {
		return leds_init_status;
	}

	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		if (!gpio_is_ready_dt(&board_leds[i].gpio)) {
			leds_initialized = true;
			leds_init_status = -ENODEV;
			return leds_init_status;
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		ret = gpio_pin_configure_dt(&board_leds[i].gpio, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			leds_initialized = true;
			leds_init_status = ret;
			return ret;
		}
	}

	leds_initialized = true;
	leds_init_status = 0;
	return 0;
}

static void start_blink_threads(void)
{
	if (blink_threads_started) {
		return;
	}

	k_tid_t yellow_tid = k_thread_create(&yellow_led_blink_thread, yellow_led_blink_stack,
					     K_THREAD_STACK_SIZEOF(yellow_led_blink_stack),
					     led_blink_thread, (void *)&board_leds[BOARD_LED_YELLOW],
					     INT_TO_POINTER(YELLOW_LED_BLINK_PERIOD_MS), NULL,
					     LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_tid_t red_tid = k_thread_create(&red_led_blink_thread, red_led_blink_stack,
					  K_THREAD_STACK_SIZEOF(red_led_blink_stack),
					  led_blink_thread, (void *)&board_leds[BOARD_LED_RED],
					  INT_TO_POINTER(RED_LED_BLINK_PERIOD_MS), NULL,
					  LED_BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(yellow_tid, "yellow_led_blink");
	k_thread_name_set(red_tid, "red_led_blink");
	blink_threads_started = true;
}

static const struct board_led *find_led(const char *color)
{
	for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
		if (strcmp(board_leds[i].color, color) == 0) {
			return &board_leds[i];
		}
	}

	return NULL;
}

static int print_led_state(const struct shell *shell, const struct board_led *led, const char *separator)
{
	int value = gpio_pin_get_dt(&led->gpio);

	if (value < 0) {
		shell_error(shell, "%s state read failed: %d", led->color, value);
		return value;
	}

	shell_print(shell, "%s%s%s", led->color, separator, value ? "on" : "off");
	return 0;
}

static int apply_led_action(const struct shell *shell, const struct board_led *led, const char *action)
{
	int ret;

	if (strcmp(action, "on") == 0) {
		ret = gpio_pin_set_dt(&led->gpio, 1);
	} else if (strcmp(action, "off") == 0) {
		ret = gpio_pin_set_dt(&led->gpio, 0);
	} else if (strcmp(action, "toggle") == 0) {
		ret = gpio_pin_toggle_dt(&led->gpio);
	} else {
		return -EINVAL;
	}

	if (ret != 0) {
		shell_error(shell, "%s %s failed: %d", action, led->color, ret);
		return ret;
	}

	return print_led_state(shell, led, " -> ");
}

static int with_selected_leds(const struct shell *shell, const char *selection,
			      int (*callback)(const struct shell *shell, const struct board_led *led,
					      const char *arg),
			      const char *arg)
{
	int ret = init_leds();

	if (ret != 0) {
		shell_error(shell, "LED initialization failed: %d", ret);
		return ret;
	}

	if (strcmp(selection, "all") == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(board_leds); ++i) {
			ret = callback(shell, &board_leds[i], arg);
			if (ret != 0) {
				return ret;
			}
		}

		return 0;
	}

	const struct board_led *led = find_led(selection);

	if (led == NULL) {
		shell_error(shell, "unknown LED color '%s'; use green, yellow, red, or all", selection);
		return -EINVAL;
	}

	return callback(shell, led, arg);
}

static int cmd_led_action(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return with_selected_leds(shell, argv[1], apply_led_action, argv[0]);
}

static int cmd_led_status(const struct shell *shell, size_t argc, char **argv)
{
	const char *selection = (argc > 1) ? argv[1] : "all";

	return with_selected_leds(shell, selection, print_led_state, ": ");
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_leds,
	SHELL_CMD_ARG(on, NULL, "Turn user LEDs on: leds on <green|yellow|red|all>",
		      cmd_led_action, 2, 0),
	SHELL_CMD_ARG(off, NULL, "Turn user LEDs off: leds off <green|yellow|red|all>",
		      cmd_led_action, 2, 0),
	SHELL_CMD_ARG(toggle, NULL, "Toggle user LEDs: leds toggle <green|yellow|red|all>",
		      cmd_led_action, 2, 0),
	SHELL_CMD_ARG(status, NULL, "Show LED state: leds status [green|yellow|red|all]",
		      cmd_led_status, 1, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(leds, &sub_leds, "Control the user LEDs (LD1/LD2/LD3)", NULL);

int main(void)
{
	int ret = init_leds();

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	if (ret != 0) {
		printf("LED initialization failed: %d\n", ret);
		return 0;
	}

	start_blink_threads();
	printf("Blink threads started for yellow (%d ms) and red (%d ms)\n",
	       YELLOW_LED_BLINK_PERIOD_MS, RED_LED_BLINK_PERIOD_MS);

	return 0;
}
