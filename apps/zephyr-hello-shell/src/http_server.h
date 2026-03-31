/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_HELLO_SHELL_HTTP_SERVER_H_
#define ZEPHYR_HELLO_SHELL_HTTP_SERVER_H_

#include <stdint.h>

int board_http_server_start(void);
uint16_t board_http_server_port(void);

#endif /* ZEPHYR_HELLO_SHELL_HTTP_SERVER_H_ */
