/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>
#include "usb_host_events.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	usb_host_events_reset();
}

ZTEST(usb_host_events, test_fifo_preserves_event_snapshots)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	usb_host_event_post(USB_HOST_EVENT_CONNECT, 0, 0);
	usb_host_event_post(USB_HOST_EVENT_URB, 2, 3);
	usb_host_event_post(USB_HOST_EVENT_DISCONNECT, 0, 0);
	zassert_true(usb_host_events_wait(K_NO_WAIT));
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.type, USB_HOST_EVENT_CONNECT);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.type, USB_HOST_EVENT_URB);
	zassert_equal(event.pipe, 2);
	zassert_equal(event.state, 3);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.type, USB_HOST_EVENT_DISCONNECT);
	zassert_false(usb_host_event_take(&event));
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.enqueued, 3);
	zassert_equal(stats.processed, 3);
	zassert_equal(stats.high_water, 3);
	zassert_equal(stats.overflows, 0);
}

ZTEST(usb_host_events, test_sof_wakes_coalesce_without_filling_fifo)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	for (int i = 0; i < 500; ++i) {
		usb_host_events_tick();
	}
	zassert_true(usb_host_events_wait(K_NO_WAIT));
	zassert_false(usb_host_events_wait(K_NO_WAIT));
	zassert_false(usb_host_event_take(&event));
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.ticks, 500);
	zassert_equal(stats.enqueued, 0);
	zassert_equal(stats.overflows, 0);
}

ZTEST(usb_host_events, test_overflow_visible_and_queued_events_preserved)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	for (int i = 0; i < USB_HOST_EVENT_QUEUE_CAPACITY; ++i) {
		usb_host_event_post(USB_HOST_EVENT_URB, i % 8, i);
	}
	usb_host_event_post(USB_HOST_EVENT_DISCONNECT, 0, 0);
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.overflows, 1);
	zassert_equal(stats.last_overflow_type, USB_HOST_EVENT_DISCONNECT);
	zassert_equal(stats.high_water, USB_HOST_EVENT_QUEUE_CAPACITY);
	for (int i = 0; i < USB_HOST_EVENT_QUEUE_CAPACITY; ++i) {
		zassert_true(usb_host_event_take(&event));
		zassert_equal(event.type, USB_HOST_EVENT_URB);
		zassert_equal(event.pipe, i % 8);
		zassert_equal(event.state, i);
	}
	zassert_false(usb_host_event_take(&event));
}

ZTEST(usb_host_events, test_intermediate_irq_wakes_coalesce_without_filling_fifo)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	for (int i = 0; i < 500; ++i) {
		usb_host_events_irq();
	}
	zassert_true(usb_host_events_wait(K_NO_WAIT));
	zassert_false(usb_host_events_wait(K_NO_WAIT));
	zassert_false(usb_host_event_take(&event));
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.irq_wakes, 500);
	zassert_equal(stats.enqueued, 0);
	zassert_equal(stats.overflows, 0);
}

ZTEST(usb_host_events, test_wait_times_out_and_late_event_wakes)
{
	zassert_false(usb_host_events_wait(K_MSEC(2)));
	usb_host_event_post(USB_HOST_EVENT_CONNECT, 0, 0);
	zassert_true(usb_host_events_wait(K_NO_WAIT));
}

static void post_from_isr(const void *arg)
{
	ARG_UNUSED(arg);
	usb_host_event_post(USB_HOST_EVENT_URB, 7, 2);
}

ZTEST(usb_host_events, test_irq_enqueue_and_latency_measurement)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	irq_offload(post_from_isr, NULL);
	zassert_true(usb_host_events_wait(K_NO_WAIT));
	k_msleep(2);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.pipe, 7);
	usb_host_events_snapshot(&stats);
	zassert_true(stats.max_latency_us >= 1000);
	usb_host_events_note_steps(8);
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.steps, 8);
}

ZTEST(usb_host_events, test_reset_clears_pending_wake_and_counters)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	usb_host_event_post(USB_HOST_EVENT_CONNECT, 0, 0);
	usb_host_events_reset();
	zassert_false(usb_host_events_wait(K_NO_WAIT));
	zassert_false(usb_host_event_take(&event));
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.enqueued, 0);
	zassert_equal(stats.processed, 0);
}

ZTEST(usb_host_events, test_repeated_urb_state_coalesces_until_next_transfer)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	usb_host_events_transfer_start(1);
	for (int i = 0; i < 500; ++i) {
		usb_host_event_post_urb(1, 2);
	}
	usb_host_event_post_urb(1, 1);
	usb_host_events_transfer_start(1);
	usb_host_event_post_urb(1, 2);
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.enqueued, 3);
	zassert_equal(stats.overflows, 0);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.state, 2);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.state, 1);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.state, 2);
	zassert_false(usb_host_event_take(&event));
}

ZTEST(usb_host_events, test_clear_metrics_preserves_fifo_and_overflow_evidence)
{
	struct usb_host_event event;
	struct usb_host_event_stats stats;

	for (int i = 0; i <= USB_HOST_EVENT_QUEUE_CAPACITY; ++i) {
		usb_host_event_post(USB_HOST_EVENT_URB, 2, 1);
	}
	k_msleep(2);
	zassert_true(usb_host_event_take(&event));
	usb_host_events_clear_metrics();
	usb_host_events_snapshot(&stats);
	zassert_equal(stats.overflows, 1);
	zassert_equal(stats.max_latency_us, 0);
	zassert_equal(stats.processed, 0);
	zassert_equal(stats.enqueued, USB_HOST_EVENT_QUEUE_CAPACITY - 1);
	zassert_true(usb_host_event_take(&event));
	zassert_equal(event.pipe, 2);
}

ZTEST_SUITE(usb_host_events, NULL, NULL, before, NULL, NULL);
