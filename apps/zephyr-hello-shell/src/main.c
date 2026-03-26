/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

struct board_led {
	const char *color;
	const char *label;
	struct gpio_dt_spec gpio;
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
	}

	return 0;
}
