/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mjpeg_multipart.h"

static void test_stream_header_contains_boundary(void)
{
	char buf[256];
	int len = mjpeg_format_stream_header(buf, sizeof(buf));

	assert(len > 0);
	assert((size_t)len == strlen(buf));
	assert(strstr(buf, "multipart/x-mixed-replace; boundary=frame") != NULL);
	assert(strstr(buf, "HTTP/1.1 200 OK") != NULL);
	printf("PASS: test_stream_header_contains_boundary\n");
}

static void test_stream_header_too_small_buffer(void)
{
	char buf[8];
	int len = mjpeg_format_stream_header(buf, sizeof(buf));

	assert(len == -1);
	printf("PASS: test_stream_header_too_small_buffer\n");
}

static void test_part_header_contains_content_length(void)
{
	char buf[256];
	int len = mjpeg_format_part_header(buf, sizeof(buf), 12345);

	assert(len > 0);
	assert((size_t)len == strlen(buf));
	assert(strstr(buf, "--frame\r\n") != NULL);
	assert(strstr(buf, "Content-Type: image/jpeg\r\n") != NULL);
	assert(strstr(buf, "Content-Length: 12345\r\n") != NULL);
	printf("PASS: test_part_header_contains_content_length\n");
}

static void test_part_header_too_small_buffer(void)
{
	char buf[8];
	int len = mjpeg_format_part_header(buf, sizeof(buf), 42);

	assert(len == -1);
	printf("PASS: test_part_header_too_small_buffer\n");
}

static void test_next_frame_index_wraps(void)
{
	assert(mjpeg_next_frame_index(0, 3) == 1);
	assert(mjpeg_next_frame_index(1, 3) == 2);
	assert(mjpeg_next_frame_index(2, 3) == 0);
	printf("PASS: test_next_frame_index_wraps\n");
}

static void test_next_frame_index_single_frame(void)
{
	assert(mjpeg_next_frame_index(0, 1) == 0);
	printf("PASS: test_next_frame_index_single_frame\n");
}

int main(void)
{
	test_stream_header_contains_boundary();
	test_stream_header_too_small_buffer();
	test_part_header_contains_content_length();
	test_part_header_too_small_buffer();
	test_next_frame_index_wraps();
	test_next_frame_index_single_frame();
	printf("ALL TESTS PASSED\n");
	return 0;
}
