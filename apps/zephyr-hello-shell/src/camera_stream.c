/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serves a looping MJPEG (multipart/x-mixed-replace) stream of the frames
 * in camera_frames.c on a dedicated TCP port and dedicated thread, isolated
 * from the Zephyr HTTP server thread that runs the port-80 dashboard
 * (http_server.c). This keeps the dashboard responsive while a client (e.g.
 * VLC) is actively viewing the stream.
 */

#include "camera_stream.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "camera_frames.h"
#include "mjpeg_multipart.h"

LOG_MODULE_REGISTER(camera_stream, LOG_LEVEL_INF);

#define CAMERA_STREAM_PORT 8081
#define CAMERA_STREAM_THREAD_STACK_SIZE 4096
#define CAMERA_STREAM_THREAD_PRIORITY 7
#define CAMERA_STREAM_FRAME_INTERVAL_MS 125 /* ~8 fps */
#define CAMERA_STREAM_HEADER_BUF_SIZE 128

K_THREAD_STACK_DEFINE(camera_stream_thread_stack, CAMERA_STREAM_THREAD_STACK_SIZE);
static struct k_thread camera_stream_thread_data;
static bool camera_stream_started;

static ssize_t camera_stream_send_all(int sock, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;

	while (len > 0) {
		ssize_t sent = zsock_send(sock, ptr, len, 0);

		if (sent < 0) {
			return sent;
		}

		ptr += (size_t)sent;
		len -= (size_t)sent;
	}

	return 0;
}

static int camera_stream_serve_client(int client_sock)
{
	char header_buf[CAMERA_STREAM_HEADER_BUF_SIZE];
	int header_len;
	size_t frame_index = 0;

	header_len = mjpeg_format_stream_header(header_buf, sizeof(header_buf));
	if (header_len < 0) {
		LOG_ERR("Stream header does not fit in buffer");
		return -ENOMEM;
	}

	if (camera_stream_send_all(client_sock, header_buf, (size_t)header_len) < 0) {
		return -errno;
	}

	while (true) {
		const struct camera_frame *frame = &camera_frames[frame_index];
		int part_header_len = mjpeg_format_part_header(header_buf, sizeof(header_buf),
								frame->len);

		if (part_header_len < 0) {
			LOG_ERR("Part header does not fit in buffer");
			return -ENOMEM;
		}

		if (camera_stream_send_all(client_sock, header_buf, (size_t)part_header_len) < 0) {
			return -errno;
		}

		if (camera_stream_send_all(client_sock, frame->data, frame->len) < 0) {
			return -errno;
		}

		if (camera_stream_send_all(client_sock, "\r\n", 2) < 0) {
			return -errno;
		}

		frame_index = mjpeg_next_frame_index(frame_index, camera_frame_count);
		k_sleep(K_MSEC(CAMERA_STREAM_FRAME_INTERVAL_MS));
	}
}

static void camera_stream_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int server_sock;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(CAMERA_STREAM_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (camera_frame_count == 0) {
		LOG_ERR("No camera frames available, stream thread exiting");
		return;
	}

	server_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_sock < 0) {
		LOG_ERR("Failed to create stream socket: %d", errno);
		return;
	}

	if (zsock_bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Failed to bind stream socket to port %d: %d", CAMERA_STREAM_PORT, errno);
		zsock_close(server_sock);
		return;
	}

	if (zsock_listen(server_sock, 1) < 0) {
		LOG_ERR("Failed to listen on stream socket: %d", errno);
		zsock_close(server_sock);
		return;
	}

	LOG_INF("Camera stream listening on port %d", CAMERA_STREAM_PORT);

	while (true) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client_sock = zsock_accept(server_sock, (struct sockaddr *)&client_addr,
					       &client_addr_len);

		if (client_sock < 0) {
			LOG_ERR("Failed to accept stream client: %d", errno);
			continue;
		}

		LOG_INF("Camera stream client connected");
		camera_stream_serve_client(client_sock);
		LOG_INF("Camera stream client disconnected");
		zsock_close(client_sock);
	}
}

int camera_stream_start(void)
{
	if (camera_stream_started) {
		return 0;
	}

	k_tid_t tid = k_thread_create(&camera_stream_thread_data, camera_stream_thread_stack,
				      K_THREAD_STACK_SIZEOF(camera_stream_thread_stack),
				      camera_stream_thread_entry, NULL, NULL, NULL,
				      CAMERA_STREAM_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "camera_stream");
	camera_stream_started = true;

	return 0;
}

uint16_t camera_stream_port(void)
{
	return CAMERA_STREAM_PORT;
}
