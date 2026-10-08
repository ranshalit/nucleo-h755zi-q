/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include "board_io.h"
#include "camera_stream.h"
#include "http_server.h"

extern int board_usb_host_start(void);

#ifdef APP_NATIVE_SIM
extern void board_io_native_sim_button_set(bool pressed);

static int cmd_sim_button(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(shell, "usage: sim button <pressed|released>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "pressed") == 0) {
		board_io_native_sim_button_set(true);
		shell_print(shell, "B1 (simulated) pressed");
	} else if (strcmp(argv[1], "released") == 0) {
		board_io_native_sim_button_set(false);
		shell_print(shell, "B1 (simulated) released");
	} else {
		shell_error(shell, "usage: sim button <pressed|released>");
		return -EINVAL;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sim,
	SHELL_CMD_ARG(button, NULL, "Set the simulated user button state",
		      cmd_sim_button, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sim, &sub_sim, "Simulation controls", NULL);
#endif

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "zephyr-hello-shell");
	shell_print(shell, "Build time: %s", APP_BUILD_TIMESTAMP);
	return 0;
}

SHELL_CMD_REGISTER(version, NULL, "Show application build information", cmd_version);

static int print_led_state(const struct shell *shell, enum board_led_id led_id, const char *separator)
{
	const struct board_led_info *led = board_led_info_get(led_id);
	bool is_on;
	int ret;

	if (led == NULL) {
		shell_error(shell, "unknown LED index %d", led_id);
		return -EINVAL;
	}

	ret = board_led_get(led_id, &is_on);
	if (ret != 0) {
		shell_error(shell, "%s state read failed: %d", led->color, ret);
		return ret;
	}

	shell_print(shell, "%s%s%s", led->color, separator, is_on ? "on" : "off");
	return 0;
}

static int apply_led_action(const struct shell *shell, enum board_led_id led_id, const char *action)
{
	const struct board_led_info *led = board_led_info_get(led_id);
	bool is_on;
	int ret;

	if (led == NULL) {
		shell_error(shell, "unknown LED index %d", led_id);
		return -EINVAL;
	}

	ret = board_led_apply_action(led_id, action, &is_on);
	if (ret != 0) {
		shell_error(shell, "%s %s failed: %d", action, led->color, ret);
		return ret;
	}

	shell_print(shell, "%s -> %s", led->color, is_on ? "on" : "off");
	return 0;
}

static int with_selected_leds(const struct shell *shell, const char *selection,
			      int (*callback)(const struct shell *shell, enum board_led_id led_id,
					      const char *arg),
			      const char *arg)
{
	int ret;

	ret = board_io_init();
	if (ret != 0) {
		shell_error(shell, "board I/O initialization failed: %d", ret);
		return ret;
	}

	if (strcmp(selection, "all") == 0) {
		for (enum board_led_id led_id = BOARD_LED_GREEN; led_id < BOARD_LED_COUNT; ++led_id) {
			ret = callback(shell, led_id, arg);
			if (ret != 0) {
				return ret;
			}
		}

		return 0;
	}

	enum board_led_id led_id;

	ret = board_led_find(selection, &led_id);
	if (ret != 0) {
		shell_error(shell, "unknown LED color '%s'; use green, yellow, red, or all",
			    selection);
		return ret;
	}

	return callback(shell, led_id, arg);
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
	const char *http_host;
	int ret = board_io_init();

#if defined(CONFIG_ARCH_POSIX)
#if defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	http_host = "127.0.0.1";
#elif defined(CONFIG_ETH_NATIVE_TAP)
	http_host = CONFIG_NET_CONFIG_MY_IPV4_ADDR;
#else
#error "native_sim requires TAP or offloaded-socket networking"
#endif
#else
	http_host = CONFIG_NET_CONFIG_MY_IPV4_ADDR;
#endif

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	printf("Build time: %s\n", APP_BUILD_TIMESTAMP);

	if (ret != 0) {
		printf("Board I/O initialization failed: %d\n", ret);
		return ret;
	}

	ret = board_blink_threads_start();
	if (ret != 0) {
		printf("Blink thread startup failed: %d\n", ret);
		return ret;
	}

	printf("Blink threads started for yellow (%u ms) and red (%u ms)\n",
	       board_led_info_get(BOARD_LED_YELLOW)->blink_period_ms,
	       board_led_info_get(BOARD_LED_RED)->blink_period_ms);

	ret = board_http_server_start();
	if (ret != 0) {
		printf("HTTP server startup failed: %d\n", ret);
		return ret;
	}

	printf("HTTP server listening on http://%s:%u/\n",
	       http_host, board_http_server_port());

	ret = board_usb_host_start();
	if (ret != 0) {
#ifdef APP_NATIVE_SIM
		if (ret == -ENOTSUP) {
			printf("USB host unsupported in simulation\n");
		} else {
			printf("USB host startup failed: %d\n", ret);
		}
#else
		printf("USB host startup failed: %d\n", ret);
#endif
		/* Non-fatal: continue running LEDs/HTTP even if USB host init failed. */
	}

	ret = camera_stream_start();
	if (ret != 0) {
		printf("Camera stream startup failed: %d\n", ret);
		/* Non-fatal: continue running LEDs/HTTP even if the stream failed. */
	} else {
		printf("Camera stream listening on http://%s:%u/\n",
		       http_host, camera_stream_port());
	}

	return 0;
}
