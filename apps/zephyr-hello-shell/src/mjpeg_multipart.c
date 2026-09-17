/* SPDX-License-Identifier: Apache-2.0 */
#include "mjpeg_multipart.h"

#include <stdio.h>

int mjpeg_format_stream_header(char *buf, size_t buf_len)
{
	int written = snprintf(buf, buf_len,
			       "HTTP/1.1 200 OK\r\n"
			       "Content-Type: multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY "\r\n"
			       "Cache-Control: no-cache\r\n"
			       "Connection: close\r\n"
			       "\r\n");

	if (written < 0 || (size_t)written >= buf_len) {
		return -1;
	}

	return written;
}

int mjpeg_format_part_header(char *buf, size_t buf_len, size_t frame_len)
{
	int written = snprintf(buf, buf_len,
			       "--" MJPEG_BOUNDARY "\r\n"
			       "Content-Type: image/jpeg\r\n"
			       "Content-Length: %zu\r\n"
			       "\r\n",
			       frame_len);

	if (written < 0 || (size_t)written >= buf_len) {
		return -1;
	}

	return written;
}

size_t mjpeg_next_frame_index(size_t current_index, size_t frame_count)
{
	size_t next = current_index + 1;

	if (next >= frame_count) {
		return 0;
	}

	return next;
}
