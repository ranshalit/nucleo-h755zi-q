/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host socket test for the port-8082 microphone stream loop: drives the real
 * audio_stream.c over loopback TCP with Zephyr APIs shimmed onto POSIX.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int test_audio_port;
#define AUDIO_PORT test_audio_port

#include "../../src/audio_stream.c"

#define SESSION 7U

static int listen_fd = -1;
/* Written by the server thread, read by the test thread. */
static atomic_uint poisoned_polls;

/* SO_REUSEADDR must precede bind() to survive TIME_WAIT from earlier runs. */
int test_zsock_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	int one = 1;

	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	return bind(fd, addr, len);
}

int test_zsock_listen(int fd, int backlog)
{
	listen_fd = fd;
	return listen(fd, backlog);
}

/*
 * The stream loop polls a two-entry array. When it polls only the listener,
 * the client entry's revents is unspecified; poison it so any read of it
 * after a fresh accept is caught.
 */
int test_zsock_poll(struct pollfd *fds, int nfds, int timeout)
{
	int ret = poll(fds, (nfds_t)nfds, timeout);

	if (nfds == 1 && fds[0].fd == listen_fd) {
		fds[1].revents = POLLERR | POLLHUP | POLLNVAL;
		atomic_fetch_add(&poisoned_polls, 1U);
	}
	return ret;
}

void usb_audio_snapshot_get(struct usb_audio_snapshot *out)
{
	memset(out, 0, sizeof(*out));
	out->connected = true;
	out->ready = true;
	out->session = SESSION;
	out->sample_rate_hz = 48000U;
	out->channels = 2U;
	out->bits_per_sample = 16U;
	out->reason = "ready";
}

size_t usb_audio_read(uint32_t session, uint8_t *out, size_t capacity)
{
	static uint8_t counter;
	size_t n = capacity < 192U ? capacity : 192U;

	if (session != SESSION) {
		return 0U;
	}
	for (size_t i = 0; i < n; ++i) {
		out[i] = counter++;
	}
	return n;
}

static void *server_main(void *arg)
{
	(void)arg;
	audio_thread_entry(NULL, NULL, NULL);
	return NULL;
}

static int connect_client(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)test_audio_port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};

	for (int attempt = 0; attempt < 200; ++attempt) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);

		assert(fd >= 0);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			return fd;
		}
		close(fd);
		k_msleep(5);
	}
	assert(!"server never listened");
	return -1;
}

/* Reads until want bytes, EOF, or timeout; returns bytes read. */
static size_t read_some(int fd, char *buf, size_t want, int timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;
	size_t got = 0;

	while (got < want) {
		struct pollfd p = {.fd = fd, .events = POLLIN};
		int left = (int)(deadline - k_uptime_get());

		if (left <= 0 || poll(&p, 1, left) <= 0) {
			break;
		}
		ssize_t n = recv(fd, buf + got, want - got, 0);

		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	return got;
}

/* Reads a whole short response until the server closes it. */
static size_t read_until_eof(int fd, char *buf, size_t cap, int timeout_ms)
{
	size_t got = read_some(fd, buf, cap - 1U, timeout_ms);

	buf[got] = '\0';
	return got;
}

static const char request[] = "GET / HTTP/1.1\r\nHost: test\r\n\r\n";

static void expect_stream_header(int fd)
{
	char buf[512];
	const char *end;
	size_t got;

	assert(send(fd, request, sizeof(request) - 1U, 0) == (ssize_t)(sizeof(request) - 1U));
	got = read_some(fd, buf, 160U, 1000);
	buf[got] = '\0';
	end = strstr(buf, "\r\n\r\n");
	assert(strncmp(buf, "HTTP/1.1 200 OK\r\n", 17) == 0);
	assert(strstr(buf, "Content-Type: application/octet-stream\r\n") != NULL);
	assert(end != NULL);
	end += 4;
	assert((size_t)(end - buf) + 16U <= got);
	assert(memcmp(end, "MIC1", 4) == 0);
	assert((uint8_t)end[12] == SESSION);
}

static void send_fragmented(int fd, const char *data, size_t length)
{
	size_t sent = 0;

	while (sent < length) {
		size_t chunk = length - sent;

		if (chunk > 113U) {
			chunk = 113U;
		}
		ssize_t n = send(fd, data + sent, chunk, MSG_NOSIGNAL);

		if (n <= 0) {
			return;
		}
		sent += (size_t)n;
		k_msleep(5);
	}
}

static void expect_stream_response(int fd)
{
	char buf[512];
	const char *end;
	size_t got = read_some(fd, buf, 160U, 1000);

	buf[got] = '\0';
	end = strstr(buf, "\r\n\r\n");
	assert(strncmp(buf, "HTTP/1.1 200 OK\r\n", 17) == 0);
	assert(end != NULL);
	end += 4;
	assert((size_t)(end - buf) + 16U <= got);
	assert(memcmp(end, "MIC1", 4) == 0);
	assert((uint8_t)end[12] == SESSION);
}

static void test_edge_request_larger_than_legacy_limit(void)
{
	static const char edge_headers[] =
		"GET / HTTP/1.1\r\n"
		"Host: 192.168.55.2:8082\r\n"
		"Connection: keep-alive\r\n"
		"sec-ch-ua-platform: \"Linux\"\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
		"(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36 Edg/141.0.0.0\r\n"
		"sec-ch-ua: \"Microsoft Edge\";v=\"141\", \"Not?A_Brand\";v=\"8\", "
		"\"Chromium\";v=\"141\"\r\n"
		"sec-ch-ua-mobile: ?0\r\n"
		"Accept: */*\r\n"
		"Origin: http://192.168.55.2\r\n"
		"Sec-Fetch-Site: same-site\r\n"
		"Sec-Fetch-Mode: cors\r\n"
		"Sec-Fetch-Dest: empty\r\n"
		"Referer: http://192.168.55.2/\r\n"
		"Accept-Encoding: gzip, deflate\r\n"
		"Accept-Language: en-US,en;q=0.9,he;q=0.8\r\n"
		"Cookie: dashboard-session=";
	char request[2048];
	size_t length = sizeof(edge_headers) - 1U;
	int fd;

	memcpy(request, edge_headers, length);
	memset(request + length, 'a', 1100U);
	length += 1100U;
	memcpy(request + length, "\r\n\r\n", 4U);
	length += 4U;
	assert(length > 1500U && length < sizeof(request));

	k_msleep(50);
	fd = connect_client();
	/* Hold the final CRLFCRLF terminator across distinct recv opportunities. */
	send_fragmented(fd, request, length - 3U);
	k_msleep(15);
	send_fragmented(fd, request + length - 3U, 3U);
	expect_stream_response(fd);
	close(fd);
	printf("PASS: fragmented ~1.5 KB Edge request returns 200 + MIC1\n");
}

static void test_oversized_request_gets_400(void)
{
	char request[8300];
	char response[512];
	static const char prefix[] = "GET / HTTP/1.1\r\nX-Padding: ";
	int fd;
	size_t length = sizeof(prefix) - 1U;
	size_t got;

	memcpy(request, prefix, length);
	memset(request + length, 'x', 8192U);
	length += 8192U;
	memcpy(request + length, "\r\n\r\n", 4U);
	length += 4U;

	k_msleep(50);
	fd = connect_client();
	send_fragmented(fd, request, length);
	got = read_until_eof(fd, response, sizeof(response), 1000);
	assert(got > 0U);
	assert(strncmp(response, "HTTP/1.1 400 Bad Request\r\n", 26) == 0);
	assert(strstr(response, "\r\n\r\nrequest too large") != NULL);
	close(fd);
	printf("PASS: request exceeding aggregate header cap gets 400\n");
}

static void test_query_path_is_accepted(void)
{
	static const char request[] = "GET /?probe=1 HTTP/1.1\r\nHost: test\r\n\r\n";
	char buf[512];
	int fd;
	size_t got;

	k_msleep(50);
	fd = connect_client();
	assert(send(fd, request, sizeof(request) - 1U, 0) == (ssize_t)(sizeof(request) - 1U));
	got = read_some(fd, buf, 160U, 1000);
	assert(got >= 17U && memcmp(buf, "HTTP/1.1 200 OK\r\n", 17) == 0);
	close(fd);
	printf("PASS: root query path is accepted\n");
}

static void test_new_client_is_served(void)
{
	int fd = connect_client();
	unsigned int before = atomic_load(&poisoned_polls);

	expect_stream_header(fd);
	assert(atomic_load(&poisoned_polls) > before);
	close(fd);
	printf("PASS: newly accepted client streams despite unpolled client slot\n");
}

static void test_busy_is_prompt_409(void)
{
	char buf[4096];
	int active;
	int second;
	int64_t start;
	size_t got;

	/* Allow the loop to notice the previous client's FIN. */
	k_msleep(50);
	active = connect_client();

	expect_stream_header(active);
	start = k_uptime_get();
	second = connect_client();
	got = read_until_eof(second, buf, sizeof(buf), 1000);
	assert(k_uptime_get() - start < 200);
	assert(got > 0U);
	assert(strncmp(buf, "HTTP/1.1 409 Conflict\r\n", 23) == 0);
	assert(strstr(buf, "Access-Control-Allow-Origin: *\r\n") != NULL);
	assert(strstr(buf, "\r\n\r\nmicrophone stream busy") != NULL);
	close(second);

	/* The active stream keeps flowing after the rejection. */
	assert(read_some(active, buf, sizeof(buf), 1000) == sizeof(buf));
	close(active);
	printf("PASS: second client gets prompt complete 409 while first streams\n");
}

static void test_slot_reused_after_close(void)
{
	/* Allow the loop to notice the previous client's FIN. */
	k_msleep(50);
	int fd = connect_client();

	expect_stream_header(fd);
	close(fd);
	printf("PASS: slot is reusable after the active client disconnects\n");
}

static void test_bad_request_gets_complete_400(void)
{
	char buf[512];
	int fd;

	k_msleep(50);
	fd = connect_client();
	assert(send(fd, "POST / HTTP/1.1\r\n\r\n", 19, 0) == 19);
	(void)read_until_eof(fd, buf, sizeof(buf), 1000);
	assert(strncmp(buf, "HTTP/1.1 400 Bad Request\r\n", 26) == 0);
	assert(strstr(buf, "Content-Length: 14\r\n") != NULL);
	assert(strstr(buf, "\r\n\r\nexpected GET /") != NULL);
	close(fd);
	printf("PASS: malformed request gets complete 400\n");
}

int main(void)
{
	pthread_t server;

	test_audio_port = 20000 + (int)(getpid() % 20000);
	assert(pthread_create(&server, NULL, server_main, NULL) == 0);
	test_new_client_is_served();
	test_busy_is_prompt_409();
	test_slot_reused_after_close();
	test_bad_request_gets_complete_400();
	test_edge_request_larger_than_legacy_limit();
	test_oversized_request_gets_400();
	test_query_path_is_accepted();
	printf("ALL TESTS PASSED\n");
	exit(0);
}
