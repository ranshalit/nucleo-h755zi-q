/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_
#define ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_

#include <stdint.h>

int camera_stream_start(void);
uint16_t camera_stream_port(void);

#endif /* ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_ */
