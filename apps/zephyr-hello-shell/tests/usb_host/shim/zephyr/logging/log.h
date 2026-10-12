/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_SHIM_ZEPHYR_LOG_H_
#define TEST_SHIM_ZEPHYR_LOG_H_

#include <stdio.h>

#define LOG_MODULE_REGISTER(...) extern int test_log_module_unused
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))

#endif /* TEST_SHIM_ZEPHYR_LOG_H_ */
