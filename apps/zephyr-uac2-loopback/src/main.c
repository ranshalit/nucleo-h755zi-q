/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB Audio Class 2 loopback demo for NUCLEO-H755ZI-Q.
 *
 * Whatever PCM audio the host plays to this device's "speaker" input
 * terminal is immediately bounced back out on the "microphone" output
 * terminal, so the same audio is heard back on the host. No I2S / ADC /
 * DAC hardware is involved -- this purely proves the USB Audio Class 2
 * isochronous data path end to end (host -> device -> host).
 *
 * Built on the modern zephyr,uac2 device class + udc_dwc2 driver, unlike
 * samples/subsys/usb/audio/headset (legacy usb_dc_stm32.c), because the
 * legacy driver never implements HAL's ISOOUTIncompleteCallback recovery
 * and silently drops all isochronous OUT data on this SoC/board (see
 * docs/usb_debug.md Step 44 for the full investigation).
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/logging/log.h>

#include "sample_usbd.h"

LOG_MODULE_REGISTER(uac2_loopback, LOG_LEVEL_INF);

#define SPEAKER_IN_TERMINAL_ID UAC2_ENTITY_ID(DT_NODELABEL(speaker_in_terminal))
#define MIC_OUT_TERMINAL_ID    UAC2_ENTITY_ID(DT_NODELABEL(mic_out_terminal))

/* Mono, 16-bit, 24 kHz -> ~24 samples/ms + 1 slack sample */
#define BYTES_PER_SLOT   2U
#define MAX_BLOCK_SIZE   ((25U) * BYTES_PER_SLOT)

/* A handful of slab buffers: 1 in flight to USB stack for receive, 1 being
 * echoed back out, plus slack for scheduling jitter.
 */
#define LOOPBACK_BLOCKS 6
K_MEM_SLAB_DEFINE_STATIC(loopback_slab, ROUND_UP(MAX_BLOCK_SIZE, UDC_BUF_GRANULARITY),
			 LOOPBACK_BLOCKS, UDC_BUF_ALIGN);

static bool speaker_enabled;
static bool mic_enabled;
static uint32_t recv_count;
static uint32_t sent_count;
static uint32_t sent_fail_count;

static void uac2_sof_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	/* No I2S clock to track -- loopback is driven entirely by data_recv_cb. */
}

static void uac2_terminal_update_cb(const struct device *dev, uint8_t terminal,
				    bool enabled, bool microframes,
				    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	__ASSERT_NO_MSG(microframes == false);

	if (terminal == SPEAKER_IN_TERMINAL_ID) {
		speaker_enabled = enabled;
		LOG_INF("Speaker terminal %s", enabled ? "enabled" : "disabled");
	} else if (terminal == MIC_OUT_TERMINAL_ID) {
		mic_enabled = enabled;
		LOG_INF("Microphone terminal %s", enabled ? "enabled" : "disabled");
	}
}

static void *uac2_get_recv_buf(const struct device *dev, uint8_t terminal,
			       uint16_t size, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	void *buf = NULL;

	if (terminal != SPEAKER_IN_TERMINAL_ID || !speaker_enabled) {
		return NULL;
	}

	if (size > MAX_BLOCK_SIZE) {
		LOG_WRN("Requested %u bytes exceeds max block size %u", size,
			MAX_BLOCK_SIZE);
		return NULL;
	}

	if (k_mem_slab_alloc(&loopback_slab, &buf, K_NO_WAIT) != 0) {
		LOG_WRN("Loopback slab exhausted, dropping block");
		return NULL;
	}

	return buf;
}

static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal,
			      void *buf, uint16_t size, void *user_data)
{
	ARG_UNUSED(user_data);

	if (terminal != SPEAKER_IN_TERMINAL_ID || !buf) {
		return;
	}

	recv_count++;
	if (recv_count == 1 || (recv_count % 200) == 0) {
		LOG_INF("recv=%u sent=%u sent_fail=%u mic_enabled=%d last_size=%u",
			recv_count, sent_count, sent_fail_count, mic_enabled, size);
	}

	if (!size || !mic_enabled) {
		k_mem_slab_free(&loopback_slab, buf);
		return;
	}

	/* Bounce the exact bytes we just received straight back out the
	 * microphone terminal -- this is the entire "loopback".
	 */
	if (usbd_uac2_send(dev, MIC_OUT_TERMINAL_ID, buf, size) < 0) {
		sent_fail_count++;
		k_mem_slab_free(&loopback_slab, buf);
	} else {
		sent_count++;
	}
}

static void uac2_buf_release_cb(const struct device *dev, uint8_t terminal,
				void *buf, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(user_data);

	k_mem_slab_free(&loopback_slab, buf);
}

static struct uac2_ops loopback_ops = {
	.sof_cb = uac2_sof_cb,
	.terminal_update_cb = uac2_terminal_update_cb,
	.get_recv_buf = uac2_get_recv_buf,
	.data_recv_cb = uac2_data_recv_cb,
	.buf_release_cb = uac2_buf_release_cb,
};

int main(void)
{
	const struct device *uac2_dev = DEVICE_DT_GET(DT_NODELABEL(uac2_loopback));
	struct usbd_context *usbd_ctx;
	int ret;

	if (!device_is_ready(uac2_dev)) {
		LOG_ERR("UAC2 device is not ready");
		return 0;
	}

	usbd_uac2_set_ops(uac2_dev, &loopback_ops, NULL);

	usbd_ctx = sample_usbd_init_device(NULL);
	if (usbd_ctx == NULL) {
		LOG_ERR("Failed to initialize USB device stack");
		return 0;
	}

	ret = usbd_enable(usbd_ctx);
	if (ret) {
		LOG_ERR("Failed to enable USB device support: %d", ret);
		return 0;
	}

	LOG_INF("USB Audio 2 loopback ready");
	return 0;
}
