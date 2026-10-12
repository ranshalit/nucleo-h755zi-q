/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_stream.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "audio_wire.h"
#include "usb_host/usb_audio_capture.h"

LOG_MODULE_REGISTER(audio_stream, LOG_LEVEL_INF);

#ifndef AUDIO_PORT
#define AUDIO_PORT 8082
#endif
#define AUDIO_STACK_SIZE 4096
#define AUDIO_POLL_MS 4
#define AUDIO_REQUEST_MS 2000
#define AUDIO_STALL_MS 250
#define AUDIO_ERROR_MS 20
#define AUDIO_REQUEST_SIZE 384
#define AUDIO_CHUNK_SIZE 2048

K_THREAD_STACK_DEFINE(audio_stack, AUDIO_STACK_SIZE);
static struct k_thread audio_thread;
static bool audio_started;

struct audio_client {
	int fd;
	char request[AUDIO_REQUEST_SIZE + 1];
	size_t request_len;
	uint8_t data[AUDIO_CHUNK_SIZE];
	size_t length;
	size_t offset;
	uint32_t session;
	int64_t deadline;
	bool streaming;
};

/*
 * Error replies are small; write them fully, but within a short bound so a
 * peer that does not read cannot stall the active stream.
 */
static void send_error(int fd, const char *status, const char *reason)
{
	char response[256];
	int64_t deadline = k_uptime_get() + AUDIO_ERROR_MS;
	size_t sent = 0;
	int n = snprintk(response, sizeof(response),
			 "HTTP/1.1 %s\r\nContent-Type: text/plain\r\n"
			 "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
			 "Connection: close\r\nContent-Length: %u\r\n\r\n%s",
			 status, (unsigned int)strlen(reason), reason);

	if (n <= 0 || (size_t)n >= sizeof(response)) {
		return;
	}
	while (sent < (size_t)n) {
		ssize_t r = zsock_send(fd, response + sent, (size_t)n - sent,
				       ZSOCK_MSG_DONTWAIT);
		struct zsock_pollfd pfd = {.fd = fd, .events = ZSOCK_POLLOUT};
		int64_t left;

		if (r > 0) {
			sent += (size_t)r;
			continue;
		}
		if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
			return;
		}
		left = deadline - k_uptime_get();
		if (left <= 0 || zsock_poll(&pfd, 1, (int)left) <= 0) {
			return;
		}
	}
}

static void close_client(struct audio_client *client)
{
	if (client->fd >= 0) {
		zsock_close(client->fd);
	}
	client->fd = -1;
	client->streaming = false;
	client->length = 0;
	client->offset = 0;
}

static bool valid_request(const char *request)
{
	return strncmp(request, "GET / HTTP/1.1\r\n", 16) == 0 ||
	       strncmp(request, "GET / HTTP/1.0\r\n", 16) == 0;
}

static void accept_request(struct audio_client *client)
{
	struct usb_audio_snapshot snapshot;
	char header[192];
	uint8_t wire[16];
	int n;

	if (!valid_request(client->request)) {
		send_error(client->fd, "400 Bad Request", "expected GET /");
		close_client(client);
		return;
	}

	usb_audio_snapshot_get(&snapshot);
	if (!snapshot.connected || !snapshot.ready || snapshot.session == 0U ||
	    snapshot.sample_rate_hz == 0U || snapshot.channels == 0U ||
	    snapshot.bits_per_sample == 0U || snapshot.bits_per_sample % 8U != 0U ||
	    snapshot.channels * (snapshot.bits_per_sample / 8U) > AUDIO_CHUNK_SIZE) {
		send_error(client->fd, "503 Service Unavailable",
			   snapshot.reason != NULL ? snapshot.reason : "microphone unavailable");
		close_client(client);
		return;
	}

	if (audio_wire_header(wire, snapshot.sample_rate_hz, snapshot.channels,
			      snapshot.bits_per_sample, snapshot.session) != 0) {
		close_client(client);
		return;
	}

	/* Start live, not with up to 250 ms of pre-connection ring contents. */
	for (size_t i = 0; i < 48; ++i) {
		if (usb_audio_read(snapshot.session, client->data,
				   sizeof(client->data)) == 0U) {
			break;
		}
	}

	n = snprintk(header, sizeof(header),
		     "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
		     "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
		     "Connection: close\r\n\r\n");
	if (n <= 0 || (size_t)n + sizeof(wire) > sizeof(client->data)) {
		close_client(client);
		return;
	}
	memcpy(client->data, header, (size_t)n);
	memcpy(client->data + n, wire, sizeof(wire));
	client->length = (size_t)n + sizeof(wire);
	client->offset = 0;
	client->session = snapshot.session;
	client->streaming = true;
	client->deadline = k_uptime_get() + AUDIO_STALL_MS;
}

static void receive_request(struct audio_client *client)
{
	ssize_t n = zsock_recv(client->fd, client->request + client->request_len,
			       AUDIO_REQUEST_SIZE - client->request_len, ZSOCK_MSG_DONTWAIT);

	if (n <= 0) {
		if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
			close_client(client);
		}
		return;
	}
	client->request_len += (size_t)n;
	client->request[client->request_len] = '\0';
	if (strstr(client->request, "\r\n\r\n") != NULL) {
		accept_request(client);
	} else if (client->request_len == AUDIO_REQUEST_SIZE) {
		send_error(client->fd, "400 Bad Request", "request too large");
		close_client(client);
	}
}

static void send_audio(struct audio_client *client)
{
	struct usb_audio_snapshot snapshot;
	ssize_t n;

	usb_audio_snapshot_get(&snapshot);
	if (!snapshot.ready || !snapshot.connected || snapshot.session != client->session) {
		close_client(client);
		return;
	}

	if (client->offset == client->length) {
		client->length = usb_audio_read(client->session, client->data,
						sizeof(client->data));
		client->offset = 0;
		if (client->length == 0U) {
			return;
		}
		client->deadline = k_uptime_get() + AUDIO_STALL_MS;
	}

	n = zsock_send(client->fd, client->data + client->offset,
		       client->length - client->offset, ZSOCK_MSG_DONTWAIT);
	if (n > 0) {
		client->offset += (size_t)n;
		client->deadline = k_uptime_get() + AUDIO_STALL_MS;
	} else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
		close_client(client);
	}
}

static void audio_thread_entry(void *p1, void *p2, void *p3)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(AUDIO_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	struct audio_client client = {.fd = -1};
	struct zsock_pollfd fds[2];
	int server;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	server = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0) {
		LOG_ERR("socket failed: %d", errno);
		return;
	}
	if (zsock_bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    zsock_listen(server, 2) < 0) {
		LOG_ERR("bind/listen failed: %d", errno);
		zsock_close(server);
		return;
	}

	LOG_INF("Microphone stream listening on port %d", AUDIO_PORT);
	while (true) {
		fds[0] = (struct zsock_pollfd){.fd = server, .events = ZSOCK_POLLIN};
		fds[1] = (struct zsock_pollfd){
			.fd = client.fd,
			.events = client.streaming && client.offset < client.length ?
					  (ZSOCK_POLLOUT | ZSOCK_POLLIN) : ZSOCK_POLLIN,
		};
		bool client_polled = client.fd >= 0;
		int count = zsock_poll(fds, client_polled ? 2 : 1, AUDIO_POLL_MS);

		if (count < 0) {
			k_msleep(AUDIO_POLL_MS);
			continue;
		}
		if (fds[0].revents & ZSOCK_POLLIN) {
			int fd = zsock_accept(server, NULL, NULL);

			if (fd >= 0) {
				struct zsock_timeval timeout = { .tv_sec = 1, .tv_usec = 0 };

				(void)zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
						       &timeout, sizeof(timeout));
				(void)zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
						       &timeout, sizeof(timeout));
				if (client.fd >= 0) {
					send_error(fd, "409 Conflict", "microphone stream busy");
					zsock_close(fd);
				} else {
					client.fd = fd;
					client.request_len = 0;
					client.length = 0;
					client.streaming = false;
					client.deadline = k_uptime_get() + AUDIO_REQUEST_MS;
				}
			}
		}
		/*
		 * fds[1].revents is only valid if the client was polled; a client
		 * accepted just now is handled on the next (immediate) poll.
		 */
		if (client.fd < 0 || !client_polled) {
			continue;
		}
		if (k_uptime_get() >= client.deadline ||
		    (fds[1].revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL))) {
			close_client(&client);
		} else if (client.streaming) {
			/* Detect FIN without waiting for a pending PCM write. */
			if (fds[1].revents & ZSOCK_POLLIN) {
				char byte;
				ssize_t n = zsock_recv(client.fd, &byte, 1,
						       ZSOCK_MSG_PEEK | ZSOCK_MSG_DONTWAIT);

				if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
					close_client(&client);
				}
			}
			if (client.fd >= 0) {
				send_audio(&client);
			}
		} else if (fds[1].revents & ZSOCK_POLLIN) {
			receive_request(&client);
		}
	}
}

int audio_stream_start(void)
{
	if (audio_started) {
		return 0;
	}

	k_tid_t tid = k_thread_create(&audio_thread, audio_stack,
				      K_THREAD_STACK_SIZEOF(audio_stack),
				      audio_thread_entry, NULL, NULL, NULL, 7, 0, K_NO_WAIT);

	k_thread_name_set(tid, "audio_stream");
	audio_started = true;
	return 0;
}
