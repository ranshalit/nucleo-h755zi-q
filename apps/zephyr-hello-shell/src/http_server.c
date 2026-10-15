/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "http_server.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/http/status.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

#include "board_io.h"
#include "board_temperature.h"
#include "usb_audio_capture.h"
#include "usb_boot_mouse.h"
#include "web_assets.h"
#include "speech_service.h"

#define JSON_RESPONSE_BUF_SIZE 1024
#define DASHBOARD_RESPONSE_BUF_SIZE 2048
#define MOUSE_RESPONSE_BUF_SIZE 256
#define MIC_RESPONSE_BUF_SIZE 384
#define LED_POST_BUF_SIZE 64
#define SPEECH_SETTINGS_POST_BUF_SIZE 64

struct led_command {
	char led[16];
	char action[16];
};

static const struct json_obj_descr led_command_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct led_command, led, JSON_TOK_STRING_BUF),
	JSON_OBJ_DESCR_PRIM(struct led_command, action, JSON_TOK_STRING_BUF),
};

struct speech_threshold_command {
	int32_t threshold_percent;
};

static const struct json_obj_descr speech_threshold_command_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct speech_threshold_command, threshold_percent, JSON_TOK_NUMBER),
};

static uint16_t board_http_port = CONFIG_APP_HTTP_PORT;
static bool http_server_started;

static const struct http_header close_connection_headers[] = {
	{
		.name = "Connection",
		.value = "close",
	},
};

static const struct http_header mouse_response_headers[] = {
	{
		.name = "Connection",
		.value = "close",
	},
	{
		.name = "Cache-Control",
		.value = "no-store",
	},
};

static const struct http_header mic_response_headers[] = {
	{ .name = "Connection", .value = "close" },
	{ .name = "Cache-Control", .value = "no-store" },
	{ .name = "Access-Control-Allow-Origin", .value = "*" },
};

struct static_asset {
	const uint8_t *data;
	const size_t *length;
};

static struct static_asset index_asset = {
	.data = board_http_index_html,
	.length = &board_http_index_html_len,
};

static struct static_asset styles_asset = {
	.data = board_http_styles_css,
	.length = &board_http_styles_css_len,
};

static struct static_asset app_js_asset = {
	.data = board_http_app_js,
	.length = &board_http_app_js_len,
};

static void prepare_close_response(struct http_client_ctx *client,
				   struct http_response_ctx *response_ctx)
{
	response_ctx->headers = close_connection_headers;
	response_ctx->header_count = ARRAY_SIZE(close_connection_headers);
	client->parser.flags |= F_CONNECTION_CLOSE;
}

HTTP_SERVICE_DEFINE(board_http_service, NULL, &board_http_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, CONFIG_HTTP_SERVER_MAX_CLIENTS, NULL, NULL);

static int static_asset_handler(struct http_client_ctx *client, enum http_data_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx, void *user_data)
{
	struct static_asset *asset = user_data;

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	prepare_close_response(client, response_ctx);
	response_ctx->body = asset->data;
	response_ctx->body_len = *asset->length;
	response_ctx->final_chunk = true;

	return 0;
}

static struct http_resource_detail_dynamic index_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/html",
		},
	.cb = static_asset_handler,
	.user_data = &index_asset,
};

static struct http_resource_detail_dynamic styles_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/css",
		},
	.cb = static_asset_handler,
	.user_data = &styles_asset,
};

static struct http_resource_detail_dynamic app_js_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/javascript",
		},
	.cb = static_asset_handler,
	.user_data = &app_js_asset,
};

static struct http_resource_detail_dynamic dashboard_styles_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/css",
		},
	.cb = static_asset_handler,
	.user_data = &styles_asset,
};

static struct http_resource_detail_dynamic dashboard_js_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/javascript",
		},
	.cb = static_asset_handler,
	.user_data = &app_js_asset,
};

static int append_to_buffer(char *buffer, size_t buffer_len, size_t *offset, const char *fmt, ...)
{
	int written;
	va_list args;

	if (*offset >= buffer_len) {
		return -ENOMEM;
	}

	va_start(args, fmt);
	written = vsnprintk(buffer + *offset, buffer_len - *offset, fmt, args);
	va_end(args);

	if (written < 0) {
		return written;
	}

	if ((size_t)written >= buffer_len - *offset) {
		*offset = buffer_len;
		return -ENOMEM;
	}

	*offset += (size_t)written;
	return 0;
}

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

static const char *network_oper_state_str(enum net_if_oper_state state)
{
	switch (state) {
	case NET_IF_OPER_NOTPRESENT:
		return "not-present";
	case NET_IF_OPER_DOWN:
		return "down";
	case NET_IF_OPER_LOWERLAYERDOWN:
		return "lower-layer-down";
	case NET_IF_OPER_TESTING:
		return "testing";
	case NET_IF_OPER_DORMANT:
		return "dormant";
	case NET_IF_OPER_UP:
		return "up";
	case NET_IF_OPER_UNKNOWN:
	default:
		return "unknown";
	}
}

static void ipv4_to_string_or_default(const struct in_addr *addr, char *buffer, size_t buffer_len)
{
	if (addr == NULL || net_addr_ntop(AF_INET, addr, buffer, buffer_len) == NULL) {
		(void)snprintk(buffer, buffer_len, "unassigned");
	}
}

static int append_leds_json(char *buffer, size_t buffer_len, size_t *offset)
{
	int ret;

	ret = append_to_buffer(buffer, buffer_len, offset, "[");
	if (ret != 0) {
		return ret;
	}

	for (enum board_led_id led_id = BOARD_LED_GREEN; led_id < BOARD_LED_COUNT; ++led_id) {
		const struct board_led_info *led = board_led_info_get(led_id);
		bool is_on = false;

		if (led == NULL) {
			return -EINVAL;
		}

		ret = board_led_get(led_id, &is_on);
		if (ret != 0) {
			return ret;
		}

		ret = append_to_buffer(
			buffer, buffer_len, offset,
			"%s{\"color\":\"%s\",\"label\":\"%s\",\"state\":\"%s\",\"blinking\":%s,"
			"\"blink_period_ms\":%u}",
			(led_id == BOARD_LED_GREEN) ? "" : ",", led->color, led->label,
			is_on ? "on" : "off", json_bool(led->blinking), led->blink_period_ms);
		if (ret != 0) {
			return ret;
		}
	}

	return append_to_buffer(buffer, buffer_len, offset, "]");
}

static int append_mouse_json(char *buffer, size_t buffer_len, size_t *offset,
			     const char *prefix)
{
	struct usb_boot_mouse_snapshot mouse;

	usb_boot_mouse_snapshot_get(&mouse);
	return append_to_buffer(
		buffer, buffer_len, offset,
		"%s{\"connected\":%s,\"session\":%u,\"reports\":%u,"
		"\"buttons\":%u,\"x\":%lld,\"y\":%lld,\"wheel\":%lld,"
		"\"presses\":{\"left\":%u,\"right\":%u,\"middle\":%u}}",
		prefix, json_bool(mouse.connected), mouse.session, mouse.reports, mouse.buttons,
		(long long)mouse.x, (long long)mouse.y, (long long)mouse.wheel,
		mouse.presses[0], mouse.presses[1], mouse.presses[2]);
}

static int append_temperature_json(char *buffer, size_t buffer_len, size_t *offset,
				   const char *prefix)
{
	struct board_temperature temperature;
	int32_t tenths;
	int32_t magnitude;

	if (board_temperature_get(&temperature) != 0) {
		return append_to_buffer(buffer, buffer_len, offset,
					"%s{\"available\":false}", prefix);
	}

	tenths = (temperature.milli_celsius >= 0) ?
			 (temperature.milli_celsius + 50) / 100 :
			 (temperature.milli_celsius - 50) / 100;
	magnitude = (tenths < 0) ? -tenths : tenths;
	return append_to_buffer(buffer, buffer_len, offset,
				"%s{\"available\":true,\"source\":\"%s\",\"celsius\":%s%d.%d}",
				prefix, temperature.source, (tenths < 0) ? "-" : "",
				(int)(magnitude / 10), (int)(magnitude % 10));
}

static int build_io_json(char *buffer, size_t buffer_len)
{
	bool button_pressed;
	size_t offset = 0U;
	int ret;

	ret = board_button_get(&button_pressed);
	if (ret != 0) {
		return ret;
	}

	ret = append_to_buffer(buffer, buffer_len, &offset,
			       "{\"button\":{\"label\":\"%s\",\"pressed\":%s},\"leds\":",
			       board_button_label(), json_bool(button_pressed));
	if (ret != 0) {
		return ret;
	}

	ret = append_leds_json(buffer, buffer_len, &offset);
	if (ret != 0) {
		return ret;
	}

	ret = append_mouse_json(buffer, buffer_len, &offset, ",\"mouse\":");
	if (ret != 0) {
		return ret;
	}

	ret = append_temperature_json(buffer, buffer_len, &offset, ",\"temperature\":");
	if (ret != 0) {
		return ret;
	}

	ret = append_to_buffer(buffer, buffer_len, &offset, "}");
	if (ret != 0) {
		return ret;
	}

	return (int)offset;
}

static int build_status_json(char *buffer, size_t buffer_len)
{
	struct net_if *iface = net_if_get_default();
	char ipv4_addr[NET_IPV4_ADDR_LEN];
	char netmask[NET_IPV4_ADDR_LEN];
	char gateway[NET_IPV4_ADDR_LEN];
	const char *peer_ipv4;
	bool admin_up = false;
	bool is_up = false;
	bool carrier = false;
	enum net_if_oper_state oper_state = NET_IF_OPER_UNKNOWN;
	static char __noinit io_json[JSON_RESPONSE_BUF_SIZE];
	struct in_addr *iface_addr = NULL;
	struct in_addr iface_netmask = { 0 };
	struct in_addr iface_gateway = { 0 };
	size_t offset = 0U;
	int ret;

	if (iface != NULL) {
		iface_addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
		if (iface_addr != NULL) {
			iface_netmask = net_if_ipv4_get_netmask_by_addr(iface, iface_addr);
		}
		iface_gateway = net_if_ipv4_get_gw(iface);
		admin_up = net_if_is_admin_up(iface);
		is_up = net_if_is_up(iface);
		carrier = net_if_is_carrier_ok(iface);
		oper_state = net_if_oper_state(iface);
	}

#if defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	peer_ipv4 = "127.0.0.1";
#elif defined(CONFIG_NET_CONFIG_PEER_IPV4_ADDR)
	peer_ipv4 = CONFIG_NET_CONFIG_PEER_IPV4_ADDR;
#else
	peer_ipv4 = "unassigned";
#endif

	ipv4_to_string_or_default(iface_addr, ipv4_addr, sizeof(ipv4_addr));
	ipv4_to_string_or_default(&iface_netmask, netmask, sizeof(netmask));
	ipv4_to_string_or_default(&iface_gateway, gateway, sizeof(gateway));

	ret = build_io_json(io_json, sizeof(io_json));
	if (ret < 0) {
		return ret;
	}

	ret = append_to_buffer(
		buffer, buffer_len, &offset,
		"{\"board\":\"%s\",\"uptime_ms\":%lld,\"http\":{\"port\":%u},\"network\":{"
		"\"ipv4\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\","
		"\"peer_ipv4\":\"%s\",\"admin_up\":%s,\"up\":%s,\"carrier\":%s,"
		"\"oper_state\":\"%s\"},\"io\":%s}",
		CONFIG_BOARD_TARGET, (long long)k_uptime_get(), board_http_port, ipv4_addr,
		netmask, gateway, peer_ipv4, json_bool(admin_up), json_bool(is_up),
		json_bool(carrier), network_oper_state_str(oper_state), io_json);
	if (ret != 0) {
		return ret;
	}

	return (int)offset;
}

static int build_led_action_json(char *buffer, size_t buffer_len, const char *selection,
				 const char *action)
{
	char io_json[JSON_RESPONSE_BUF_SIZE];
	int io_len;
	int ret;

	io_len = build_io_json(io_json, sizeof(io_json));
	if (io_len < 0) {
		return io_len;
	}

	ret = snprintk(buffer, buffer_len,
		       "{\"selection\":\"%s\",\"action\":\"%s\",\"io\":%s}",
		       selection, action, io_json);
	if (ret < 0) {
		return ret;
	}

	if ((size_t)ret >= buffer_len) {
		return -ENOMEM;
	}

	return ret;
}

static int build_dashboard_json(char *buffer, size_t buffer_len)
{
	static char __noinit status_json[JSON_RESPONSE_BUF_SIZE];
	static char __noinit io_json[JSON_RESPONSE_BUF_SIZE];
	int status_len;
	int io_len;
	int ret;

	status_len = build_status_json(status_json, sizeof(status_json));
	if (status_len < 0) {
		return status_len;
	}

	io_len = build_io_json(io_json, sizeof(io_json));
	if (io_len < 0) {
		return io_len;
	}

	ret = snprintk(buffer, buffer_len, "{\"status\":%s,\"io\":%s}", status_json, io_json);
	if (ret < 0) {
		return ret;
	}

	if ((size_t)ret >= buffer_len) {
		return -ENOMEM;
	}

	return ret;
}

static void populate_response(struct http_client_ctx *client, struct http_response_ctx *response_ctx,
			      enum http_status status, const uint8_t *body, size_t body_len)
{
	response_ctx->status = status;
	prepare_close_response(client, response_ctx);
	response_ctx->body = body;
	response_ctx->body_len = body_len;
	response_ctx->final_chunk = true;
}

static int send_internal_error(struct http_client_ctx *client, struct http_response_ctx *response_ctx,
			       char *buffer, size_t buffer_len)
{
	int ret = snprintk(buffer, buffer_len, "{\"error\":\"internal server error\"}");

	if (ret < 0) {
		return ret;
	}

	if ((size_t)ret >= buffer_len) {
		return -ENOMEM;
	}

	populate_response(client, response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, buffer, (size_t)ret);
	return 0;
}

static int send_bad_request(struct http_client_ctx *client, struct http_response_ctx *response_ctx,
			    char *buffer, size_t buffer_len, const char *message)
{
	int ret = snprintk(buffer, buffer_len, "{\"error\":\"%s\"}", message);

	if (ret < 0) {
		return ret;
	}

	if ((size_t)ret >= buffer_len) {
		return -ENOMEM;
	}

	populate_response(client, response_ctx, HTTP_400_BAD_REQUEST, buffer, (size_t)ret);
	return 0;
}

static int status_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit status_response[JSON_RESPONSE_BUF_SIZE];
	int ret;

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	ret = build_status_json(status_response, sizeof(status_response));
	if (ret < 0) {
		return send_internal_error(client, response_ctx, status_response,
					 sizeof(status_response));
	}

	populate_response(client, response_ctx, HTTP_200_OK, status_response, (size_t)ret);
	return 0;
}

static struct http_resource_detail_dynamic status_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = status_handler,
	.user_data = NULL,
};

static int io_handler(struct http_client_ctx *client, enum http_data_status status,
		      const struct http_request_ctx *request_ctx,
		      struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit io_response[JSON_RESPONSE_BUF_SIZE];
	int ret;

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	ret = build_io_json(io_response, sizeof(io_response));
	if (ret < 0) {
		return send_internal_error(client, response_ctx, io_response, sizeof(io_response));
	}

	populate_response(client, response_ctx, HTTP_200_OK, io_response, (size_t)ret);
	return 0;
}

static struct http_resource_detail_dynamic io_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = io_handler,
	.user_data = NULL,
};

static int mouse_handler(struct http_client_ctx *client, enum http_data_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit mouse_response[MOUSE_RESPONSE_BUF_SIZE];
	size_t offset = 0U;
	int ret;

	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	ret = append_mouse_json(mouse_response, sizeof(mouse_response), &offset, "");
	if (ret != 0) {
		return send_internal_error(client, response_ctx, mouse_response,
					   sizeof(mouse_response));
	}

	populate_response(client, response_ctx, HTTP_200_OK, mouse_response, offset);
	response_ctx->headers = mouse_response_headers;
	response_ctx->header_count = ARRAY_SIZE(mouse_response_headers);
	return 0;
}

static struct http_resource_detail_dynamic mouse_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = mouse_handler,
	.user_data = NULL,
};

static int mic_handler(struct http_client_ctx *client, enum http_data_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit response[MIC_RESPONSE_BUF_SIZE];
	struct usb_audio_snapshot mic;
	size_t offset = 0;
	const char *reason;
	int ret;

	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	usb_audio_snapshot_get(&mic);
	reason = mic.reason != NULL ? mic.reason : "microphone unavailable";
	ret = append_to_buffer(response, sizeof(response), &offset,
			       "{\"attached\":%s,\"ready\":%s,\"session\":%u,"
			       "\"format\":{\"sample_rate_hz\":%u,\"channels\":%u,"
			       "\"bits_per_sample\":%u},\"level\":%u,\"packets\":%u,"
			       "\"drops\":%u,\"errors\":%u,\"missed\":%u,\"reason\":\"",
			       json_bool(mic.connected), json_bool(mic.ready), mic.session,
			       mic.sample_rate_hz, mic.channels, mic.bits_per_sample,
			       mic.level, mic.packets, mic.drops, mic.errors, mic.missed);
	if (ret != 0) {
		goto overflow;
	}
	/* Escape the diagnostic string rather than trusting USB-facing state as JSON. */
	for (const unsigned char *p = (const unsigned char *)reason; *p != '\0'; ++p) {
		if (*p == '"' || *p == '\\') {
			ret = append_to_buffer(response, sizeof(response), &offset, "\\%c", *p);
		} else if (*p < 0x20U) {
			ret = append_to_buffer(response, sizeof(response), &offset, "\\u%04x", *p);
		} else {
			ret = append_to_buffer(response, sizeof(response), &offset, "%c", *p);
		}
		if (ret != 0) {
			goto overflow;
		}
	}
	ret = append_to_buffer(response, sizeof(response), &offset, "\"}");
	if (ret != 0) {
		goto overflow;
	}
	populate_response(client, response_ctx, HTTP_200_OK, response, offset);
	response_ctx->headers = mic_response_headers;
	response_ctx->header_count = ARRAY_SIZE(mic_response_headers);
	return 0;

overflow:
	ret = send_internal_error(client, response_ctx, response, sizeof(response));
	if (ret == 0) {
		response_ctx->headers = mic_response_headers;
		response_ctx->header_count = ARRAY_SIZE(mic_response_headers);
	}
	return ret;
}

static struct http_resource_detail_dynamic mic_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = mic_handler,
};

static int speech_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit response[3072];
	struct speech_snapshot snapshot;
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);
	if (status != HTTP_SERVER_DATA_FINAL) { return 0; }
	speech_snapshot_get(&snapshot);
	int ret = speech_snapshot_json(&snapshot, response, sizeof(response));
	if (ret < 0) {
		return send_internal_error(client, response_ctx, response, sizeof(response));
	}
	populate_response(client, response_ctx, HTTP_200_OK, response, (size_t)ret);
	response_ctx->headers = mouse_response_headers;
	response_ctx->header_count = ARRAY_SIZE(mouse_response_headers);
	return 0;
}

static struct http_resource_detail_dynamic speech_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = speech_handler,
};

static int speech_settings_handler(struct http_client_ctx *client, enum http_data_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit response[128];
	static uint8_t __noinit post_buffer[SPEECH_SETTINGS_POST_BUF_SIZE];
	static size_t cursor;
	struct speech_threshold_command cmd = { 0 };
	const int expected_parse_mask = BIT_MASK(ARRAY_SIZE(speech_threshold_command_descr));

	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0U;
		return 0;
	}

	if (cursor > sizeof(post_buffer) ||
	    request_ctx->data_len > sizeof(post_buffer) - cursor) {
		cursor = 0U;
		return send_bad_request(client, response_ctx, response, sizeof(response),
					"request body too large");
	}

	if (request_ctx->data_len != 0U) {
		memcpy(post_buffer + cursor, request_ctx->data, request_ctx->data_len);
		cursor += request_ctx->data_len;
	}

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	int ret = json_obj_parse(post_buffer, cursor, speech_threshold_command_descr,
				 ARRAY_SIZE(speech_threshold_command_descr), &cmd);
	cursor = 0U;
	if (ret != expected_parse_mask) {
		return send_bad_request(client, response_ctx, response, sizeof(response),
					"expected integer threshold_percent from 50 to 100");
	}
	if (cmd.threshold_percent < 50 || cmd.threshold_percent > 100) {
		return send_bad_request(client, response_ctx, response, sizeof(response),
					"threshold_percent must be from 50 to 100");
	}

	ret = speech_service_set_threshold((uint8_t)cmd.threshold_percent);
	if (ret == -ERANGE) {
		return send_bad_request(client, response_ctx, response, sizeof(response),
					"threshold_percent must be from 50 to 100");
	}
	if (ret != 0) {
		return send_internal_error(client, response_ctx, response, sizeof(response));
	}

	ret = snprintk(response, sizeof(response), "{\"threshold_percent\":%d}",
		       cmd.threshold_percent);
	if (ret < 0 || (size_t)ret >= sizeof(response)) {
		return send_internal_error(client, response_ctx, response, sizeof(response));
	}

	populate_response(client, response_ctx, HTTP_200_OK, response, (size_t)ret);
	response_ctx->headers = mouse_response_headers;
	response_ctx->header_count = ARRAY_SIZE(mouse_response_headers);
	return 0;
}

static struct http_resource_detail_dynamic speech_settings_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = speech_settings_handler,
};

static int dashboard_handler(struct http_client_ctx *client, enum http_data_status status,
			     const struct http_request_ctx *request_ctx,
			     struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit dashboard_response[DASHBOARD_RESPONSE_BUF_SIZE];
	int ret;

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	ret = build_dashboard_json(dashboard_response, sizeof(dashboard_response));
	if (ret < 0) {
		return send_internal_error(client, response_ctx, dashboard_response,
					   sizeof(dashboard_response));
	}

	populate_response(client, response_ctx, HTTP_200_OK, dashboard_response, (size_t)ret);
	return 0;
}

static struct http_resource_detail_dynamic dashboard_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = dashboard_handler,
	.user_data = NULL,
};

static int apply_led_action_to_selection(const char *selection, const char *action)
{
	int ret;

	if (strcmp(selection, "all") == 0) {
		for (enum board_led_id led_id = BOARD_LED_GREEN; led_id < BOARD_LED_COUNT; ++led_id) {
			ret = board_led_apply_action(led_id, action, NULL);
			if (ret != 0) {
				return ret;
			}
		}

		return 0;
	}

	enum board_led_id led_id;

	ret = board_led_find(selection, &led_id);
	if (ret != 0) {
		return ret;
	}

	return board_led_apply_action(led_id, action, NULL);
}

static int leds_handler(struct http_client_ctx *client, enum http_data_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	static char __noinit response_buffer[JSON_RESPONSE_BUF_SIZE];
	static uint8_t __noinit post_buffer[LED_POST_BUF_SIZE];
	static size_t cursor;
	struct led_command cmd = { 0 };
	const int expected_parse_mask = BIT_MASK(ARRAY_SIZE(led_command_descr));
	int ret;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0U;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_buffer)) {
		cursor = 0U;
		return send_bad_request(client, response_ctx, response_buffer, sizeof(response_buffer),
					"request body too large");
	}

	memcpy(post_buffer + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	ret = json_obj_parse(post_buffer, cursor, led_command_descr, ARRAY_SIZE(led_command_descr), &cmd);
	cursor = 0U;
	if (ret != expected_parse_mask) {
		return send_bad_request(client, response_ctx, response_buffer, sizeof(response_buffer),
					"expected JSON body with led and action fields");
	}

	ret = apply_led_action_to_selection(cmd.led, cmd.action);
	if (ret == -ENOENT) {
		return send_bad_request(client, response_ctx, response_buffer, sizeof(response_buffer),
					"unknown led selection");
	}

	if (ret == -EINVAL) {
		return send_bad_request(client, response_ctx, response_buffer, sizeof(response_buffer),
					"unknown action");
	}

	if (ret != 0) {
		return send_internal_error(client, response_ctx, response_buffer, sizeof(response_buffer));
	}

	ret = build_led_action_json(response_buffer, sizeof(response_buffer), cmd.led, cmd.action);
	if (ret < 0) {
		return send_internal_error(client, response_ctx, response_buffer, sizeof(response_buffer));
	}

	populate_response(client, response_ctx, HTTP_200_OK, response_buffer, (size_t)ret);
	return 0;
}

static struct http_resource_detail_dynamic leds_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
			.content_type = "application/json",
		},
	.cb = leds_handler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(root_resource, board_http_service, "/", &index_resource_detail);
HTTP_RESOURCE_DEFINE(styles_resource, board_http_service, "/styles.css", &styles_resource_detail);
HTTP_RESOURCE_DEFINE(app_js_resource, board_http_service, "/app.js", &app_js_resource_detail);
HTTP_RESOURCE_DEFINE(dashboard_styles_resource, board_http_service, "/dashboard.css",
		     &dashboard_styles_resource_detail);
HTTP_RESOURCE_DEFINE(dashboard_js_resource, board_http_service, "/dashboard.js",
		     &dashboard_js_resource_detail);
HTTP_RESOURCE_DEFINE(dashboard_api_resource, board_http_service, "/api/v1/dashboard",
		     &dashboard_resource_detail);
HTTP_RESOURCE_DEFINE(status_resource, board_http_service, "/api/v1/status", &status_resource_detail);
HTTP_RESOURCE_DEFINE(io_resource, board_http_service, "/api/v1/io", &io_resource_detail);
HTTP_RESOURCE_DEFINE(mouse_resource, board_http_service, "/api/v1/mouse", &mouse_resource_detail);
HTTP_RESOURCE_DEFINE(mic_resource, board_http_service, "/api/v1/mic", &mic_resource_detail);
HTTP_RESOURCE_DEFINE(speech_resource, board_http_service, "/api/v1/speech", &speech_resource_detail);
HTTP_RESOURCE_DEFINE(speech_settings_resource, board_http_service, "/api/v1/speech/settings",
		     &speech_settings_resource_detail);
HTTP_RESOURCE_DEFINE(leds_resource, board_http_service, "/api/v1/leds", &leds_resource_detail);

int board_http_server_start(void)
{
	int ret;

	if (http_server_started) {
		return 0;
	}

	ret = board_io_init();
	if (ret != 0) {
		return ret;
	}

	ret = http_server_start();
	if (ret == 0) {
		http_server_started = true;
	}

	return ret;
}

uint16_t board_http_server_port(void)
{
	return board_http_port;
}
