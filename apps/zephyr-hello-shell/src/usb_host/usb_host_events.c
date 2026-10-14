/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_host_events.h"
#include <string.h>

K_MSGQ_DEFINE(usb_host_event_queue, sizeof(struct usb_host_event),
	      USB_HOST_EVENT_QUEUE_CAPACITY, 4);
K_SEM_DEFINE(usb_host_event_wake, 0, 1);
static struct k_spinlock event_lock;
static struct usb_host_event_stats stats;
static uint8_t last_urb_state[16];

void usb_host_events_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	k_msgq_purge(&usb_host_event_queue);
	k_sem_reset(&usb_host_event_wake);
	memset(&stats, 0, sizeof(stats));
	memset(last_urb_state, 0, sizeof(last_urb_state));
	k_spin_unlock(&event_lock, key);
}

void usb_host_events_clear_metrics(void)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);
	uint32_t overflows = stats.overflows;
	uint32_t last_overflow_type = stats.last_overflow_type;

	memset(&stats, 0, sizeof(stats));
	stats.overflows = overflows;
	stats.last_overflow_type = last_overflow_type;
	stats.enqueued = k_msgq_num_used_get(&usb_host_event_queue);
	stats.high_water = stats.enqueued;
	k_spin_unlock(&event_lock, key);
}

static void enqueue_locked(enum usb_host_event_type type, uint8_t pipe, uint8_t state)
{
	struct usb_host_event event = {
		.type = type,
		.cycles = k_cycle_get_32(),
		.pipe = pipe,
		.state = state,
	};
	if (type == USB_HOST_EVENT_IRQ) {
		++stats.irq_events;
	} else if (type == USB_HOST_EVENT_URB) {
		++stats.urb_events;
	}
	if (k_msgq_put(&usb_host_event_queue, &event, K_NO_WAIT) != 0) {
		++stats.overflows;
		stats.last_overflow_type = type;
	} else {
		++stats.enqueued;
		stats.high_water = MAX(stats.high_water,
				       k_msgq_num_used_get(&usb_host_event_queue));
	}
}

void usb_host_event_post(enum usb_host_event_type type, uint8_t pipe, uint8_t state)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	enqueue_locked(type, pipe, state);
	k_spin_unlock(&event_lock, key);
	k_sem_give(&usb_host_event_wake);
}

void usb_host_events_transfer_start(uint8_t pipe)
{
	__ASSERT_NO_MSG(pipe < ARRAY_SIZE(last_urb_state));
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	last_urb_state[pipe] = 0; /* HAL URB_IDLE on each new submission. */
	k_spin_unlock(&event_lock, key);
}

void usb_host_event_post_urb(uint8_t pipe, uint8_t state)
{
	__ASSERT_NO_MSG(pipe < ARRAY_SIZE(last_urb_state));
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	/* HAL control NAK retries repeatedly notify the same live URB state. */
	if (last_urb_state[pipe] == state) {
		++stats.repeated_urbs;
	} else {
		last_urb_state[pipe] = state;
		enqueue_locked(USB_HOST_EVENT_URB, pipe, state);
	}
	k_spin_unlock(&event_lock, key);
	k_sem_give(&usb_host_event_wake);
}

void usb_host_events_tick(void)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	++stats.ticks;
	k_spin_unlock(&event_lock, key);
	k_sem_give(&usb_host_event_wake);
}

void usb_host_events_irq(void)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	++stats.irq_wakes;
	k_spin_unlock(&event_lock, key);
	k_sem_give(&usb_host_event_wake);
}

bool usb_host_events_wait(k_timeout_t timeout)
{
	bool woke = k_sem_take(&usb_host_event_wake, timeout) == 0;
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	if (woke) {
		++stats.wakeups;
	} else {
		++stats.timeouts;
	}
	k_spin_unlock(&event_lock, key);
	return woke;
}

bool usb_host_event_take(struct usb_host_event *event)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);
	bool taken = k_msgq_get(&usb_host_event_queue, event, K_NO_WAIT) == 0;

	if (taken) {
		uint32_t latency_us = k_cyc_to_us_floor32(k_cycle_get_32() -
							 event->cycles);

		++stats.processed;
		stats.max_latency_us = MAX(stats.max_latency_us, latency_us);
	}
	k_spin_unlock(&event_lock, key);
	return taken;
}

void usb_host_events_note_steps(uint32_t steps)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	stats.steps += steps;
	k_spin_unlock(&event_lock, key);
}

void usb_host_events_snapshot(struct usb_host_event_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&event_lock);

	*out = stats;
	k_spin_unlock(&event_lock, key);
}
