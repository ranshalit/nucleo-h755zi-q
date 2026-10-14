/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_HOST_EVENTS_H_
#define USB_HOST_EVENTS_H_

#include <zephyr/kernel.h>

#define USB_HOST_EVENT_QUEUE_CAPACITY 128U

enum usb_host_event_type {
	USB_HOST_EVENT_CONNECT,
	USB_HOST_EVENT_DISCONNECT,
	USB_HOST_EVENT_PORT_ENABLED,
	USB_HOST_EVENT_PORT_DISABLED,
	USB_HOST_EVENT_URB,
	USB_HOST_EVENT_IRQ,
};

struct usb_host_event {
	enum usb_host_event_type type;
	uint32_t cycles;
	uint8_t pipe;
	uint8_t state;
};

struct usb_host_event_stats {
	uint32_t enqueued;
	uint32_t processed;
	uint32_t overflows;
	uint32_t high_water;
	uint32_t ticks;
	uint32_t wakeups;
	uint32_t timeouts;
	uint32_t steps;
	uint32_t max_latency_us;
	uint32_t irq_events;
	uint32_t urb_events;
	uint32_t last_overflow_type;
	uint32_t irq_wakes;
	uint32_t repeated_urbs;
};

void usb_host_events_reset(void);
void usb_host_events_clear_metrics(void);
void usb_host_event_post(enum usb_host_event_type type, uint8_t pipe, uint8_t state);
void usb_host_events_transfer_start(uint8_t pipe);
void usb_host_event_post_urb(uint8_t pipe, uint8_t state);
void usb_host_events_tick(void);
void usb_host_events_irq(void);
bool usb_host_events_wait(k_timeout_t timeout);
bool usb_host_event_take(struct usb_host_event *event);
void usb_host_events_note_steps(uint32_t steps);
void usb_host_events_snapshot(struct usb_host_event_stats *out);

#endif
