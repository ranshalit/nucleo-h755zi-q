/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal host stand-in for the Zephyr kernel API used by audio_stream.c. */
#ifndef TEST_SHIM_ZEPHYR_KERNEL_H_
#define TEST_SHIM_ZEPHYR_KERNEL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define K_THREAD_STACK_DEFINE(name, size) static char name[size]
#define K_THREAD_STACK_SIZEOF(stack) sizeof(stack)
#define K_NO_WAIT 0
#define ARG_UNUSED(x) (void)(x)
#define snprintk snprintf

struct k_thread {
	int unused;
};
typedef struct k_thread *k_tid_t;
typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

static inline k_tid_t k_thread_create(struct k_thread *thread, char *stack, size_t size,
				      k_thread_entry_t entry, void *p1, void *p2, void *p3,
				      int prio, uint32_t options, int delay)
{
	(void)stack; (void)size; (void)entry; (void)p1; (void)p2; (void)p3;
	(void)prio; (void)options; (void)delay;
	return thread;
}

static inline void k_thread_name_set(k_tid_t tid, const char *name)
{
	(void)tid;
	(void)name;
}

static inline int64_t k_uptime_get(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static inline int32_t k_msleep(int32_t ms)
{
	struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};

	nanosleep(&ts, NULL);
	return 0;
}

#endif /* TEST_SHIM_ZEPHYR_KERNEL_H_ */
