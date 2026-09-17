/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MJPEG_MULTIPART_H_
#define MJPEG_MULTIPART_H_

#include <stddef.h>

#define MJPEG_BOUNDARY "frame"

/* Formats the initial HTTP response line + multipart content-type header
 * into buf. Returns the number of bytes written (excluding the terminating
 * NUL), or -1 if buf_len is too small to hold the header. */
int mjpeg_format_stream_header(char *buf, size_t buf_len);

/* Formats one multipart part header (boundary marker + Content-Type +
 * Content-Length + blank line) for a frame of the given length into buf.
 * Returns the number of bytes written (excluding the terminating NUL), or
 * -1 if buf_len is too small. */
int mjpeg_format_part_header(char *buf, size_t buf_len, size_t frame_len);

/* Returns the next frame index to send, wrapping back to 0 once
 * current_index reaches frame_count - 1. frame_count must be > 0. */
size_t mjpeg_next_frame_index(size_t current_index, size_t frame_count);

#endif /* MJPEG_MULTIPART_H_ */
