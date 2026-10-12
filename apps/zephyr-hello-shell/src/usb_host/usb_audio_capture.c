/* SPDX-License-Identifier: Apache-2.0 */
/*
 * STM32 USBH class that streams a UAC1 microphone (isochronous IN) into
 * a bounded PCM ring. Producer: the HCD interrupt completes and resubmits
 * each iso IN (usb_audio_capture_urb_done()); the USB poll thread
 * (BgndProcess) starts the stream and handles errors, missed frames and
 * halts. Consumer: usb_audio_read() from any other thread.
 */
#include "usb_audio_capture.h"
#include "usb_audio_desc.h"
#include "usb_class_select.h"
#include "stm32_hcd_adapter.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "usbh_core.h"
#include "usbh_ctlreq.h"
#include "usbh_pipes.h"

#define AUDIO_CLASS 0x01U
#define AUDIO_REQ_SET_CUR 0x01U
#define AUDIO_REQ_GET_CUR 0x81U
#define AUDIO_SAMPLING_FREQ_CONTROL 0x01U
#define CS_ENDPOINT 0x25U
#define EP_GENERAL 0x01U
#define AUDIO_MAX_PACKET 1023U
/* Frames to wait for a completed iso IN before declaring it missed. */
#define AUDIO_URB_MISSED_FRAMES 2U
/* Frames before a still-enabled channel is forcibly halted. */
#define AUDIO_URB_HALT_FRAMES 8U
#define AUDIO_MAX_CONSECUTIVE_ERRORS 200U

enum audio_request {
	AUDIO_REQ_STEP_INTERFACE,
	AUDIO_REQ_STEP_SET_RATE,
	AUDIO_REQ_STEP_GET_RATE,
	AUDIO_REQ_STEP_DONE,
};

struct audio_state {
	struct usb_audio_format format;
	uint8_t pipe;
	bool allocated;
	bool pending;
	bool halting;
	bool failed;
	bool rate_control;
	enum audio_request request;
	uint32_t submit_timer;
	uint32_t generation;
	uint32_t consecutive_errors;
	uint8_t rate[4] __aligned(4);
	uint8_t packet[AUDIO_MAX_PACKET + 1U] __aligned(4);
};

static struct audio_state audio;
static struct usb_audio_ring audio_ring;
static struct k_spinlock audio_lock;
static bool audio_ring_initialized;

static void ring_lazy_init(void)
{
	if (!audio_ring_initialized) {
		usb_audio_ring_init(&audio_ring, "no audio device");
		audio_ring_initialized = true;
	}
}

void usb_audio_snapshot_get(struct usb_audio_snapshot *out)
{
	if (out == NULL) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	ring_lazy_init();
	usb_audio_ring_snapshot(&audio_ring, out);
	k_spin_unlock(&audio_lock, key);
}

size_t usb_audio_read(uint32_t session, uint8_t *out, size_t capacity)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	size_t n;

	ring_lazy_init();
	n = usb_audio_ring_read(&audio_ring, session, out, capacity);
	k_spin_unlock(&audio_lock, key);
	return n;
}

static void audio_set_ready(bool ready, const char *reason)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	usb_audio_ring_set_ready(&audio_ring, ready, reason);
	k_spin_unlock(&audio_lock, key);
}

static USBH_StatusTypeDef audio_init_failed(USBH_HandleTypeDef *host,
					     const char *reason)
{
	if (audio.allocated) {
		(void)USBH_FreePipe(host, audio.pipe);
		audio.allocated = false;
	}

	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	usb_audio_ring_detach(&audio_ring, reason);
	k_spin_unlock(&audio_lock, key);
	return USBH_FAIL;
}

/*
 * True when the selected streaming alternate advertises the UAC1
 * sampling-frequency control on its data endpoint (CS_ENDPOINT
 * EP_GENERAL bmAttributes bit 0). The descriptor was already validated
 * by usb_audio_select_input().
 */
static bool has_rate_control(const uint8_t *raw, uint16_t total,
			     const struct usb_audio_format *format)
{
	bool in_alt = false;
	bool after_ep = false;

	for (uint16_t off = raw[0]; off + 2U <= total && raw[off] >= 2U;
	     off += raw[off]) {
		uint8_t len = raw[off];
		uint8_t type = raw[off + 1U];

		if ((uint32_t)off + len > total) {
			break;
		}
		if (type == USB_DESC_TYPE_INTERFACE && len >= 9U) {
			in_alt = raw[off + 2U] == format->interface_number &&
				 raw[off + 3U] == format->alternate_setting;
			after_ep = false;
		} else if (type == USB_DESC_TYPE_ENDPOINT && len >= 7U) {
			after_ep = in_alt && raw[off + 2U] == format->endpoint;
		} else if (type == CS_ENDPOINT && after_ep && len >= 7U &&
			   raw[off + 2U] == EP_GENERAL) {
			return (raw[off + 3U] & 0x01U) != 0U;
		}
	}
	return false;
}

static USBH_StatusTypeDef audio_init(USBH_HandleTypeDef *host)
{
	struct usb_function_selection sel;
	const uint8_t *raw = host->device.CfgDesc_Raw;

	memset(&audio, 0, sizeof(audio));
	if (usb_function_select(raw, sizeof(host->device.CfgDesc_Raw), &sel) != 0 ||
	    sel.kind != USB_FUNCTION_AUDIO_IN ||
	    sel.audio.packet_bytes > AUDIO_MAX_PACKET) {
		printf("ST USBH: no supported audio input function\n");
		return USBH_FAIL;
	}
	audio.format = sel.audio;
	audio.rate_control = has_rate_control(raw, (uint16_t)(raw[2] | (raw[3] << 8)),
					      &audio.format);

	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	uint32_t session;

	ring_lazy_init();
	session = usb_audio_ring_attach(&audio_ring, audio.format.sample_rate_hz,
					audio.format.channels,
					audio.format.bits_per_sample);
	k_spin_unlock(&audio_lock, key);
	if (session == 0U) {
		printf("ST USBH: audio format not supported by capture ring\n");
		return USBH_FAIL;
	}

	audio.pipe = USBH_AllocPipe(host, audio.format.endpoint);
	if (audio.pipe == 0xffU) {
		return audio_init_failed(host, "no free host channel");
	}
	audio.allocated = true;
	if (USBH_LL_OpenPipe(host, audio.pipe, audio.format.endpoint,
			     host->device.address, host->device.speed, USBH_EP_ISO,
			     audio.format.packet_bytes) != USBH_OK) {
		return audio_init_failed(host, "iso pipe open failed");
	}
	printf("ST USBH: audio IN IF%u alt%u EP 0x%02x %u Hz %u ch %u-bit "
	       "packet %u B rate_control=%u session %u\n",
	       audio.format.interface_number, audio.format.alternate_setting,
	       audio.format.endpoint, audio.format.sample_rate_hz,
	       audio.format.channels, audio.format.bits_per_sample,
	       audio.format.packet_bytes, audio.rate_control, session);
	return USBH_OK;
}

static USBH_StatusTypeDef audio_deinit(USBH_HandleTypeDef *host)
{
	unsigned int irq_key = irq_lock();
	bool allocated = audio.allocated;

	/* Stop the interrupt-side resubmission before closing the pipe. */
	audio.allocated = false;
	irq_unlock(irq_key);
	if (allocated) {
		(void)USBH_ClosePipe(host, audio.pipe);
		(void)USBH_FreePipe(host, audio.pipe);
	}
	memset(&audio, 0, sizeof(audio));

	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	ring_lazy_init();
	usb_audio_ring_detach(&audio_ring, "disconnected");
	k_spin_unlock(&audio_lock, key);
	return USBH_OK;
}

static void rate_request(USBH_HandleTypeDef *host, uint8_t type, uint8_t request)
{
	host->Control.setup.b.bmRequestType = type;
	host->Control.setup.b.bRequest = request;
	host->Control.setup.b.wValue.w = AUDIO_SAMPLING_FREQ_CONTROL << 8;
	host->Control.setup.b.wIndex.w = audio.format.endpoint;
	host->Control.setup.b.wLength.w = 3U;
}

static USBH_StatusTypeDef audio_requests(USBH_HandleTypeDef *host)
{
	USBH_StatusTypeDef result;
	uint32_t rate;

	switch (audio.request) {
	case AUDIO_REQ_STEP_INTERFACE:
		result = USBH_SetInterface(host, audio.format.interface_number,
					   audio.format.alternate_setting);
		if (result == USBH_OK) {
			audio.request = audio.rate_control ? AUDIO_REQ_STEP_SET_RATE :
							     AUDIO_REQ_STEP_DONE;
		} else if (result != USBH_BUSY) {
			printf("ST USBH: audio SET_INTERFACE failed: %u\n", result);
			audio_set_ready(false, "set interface failed");
			return USBH_FAIL;
		}
		return USBH_BUSY;

	case AUDIO_REQ_STEP_SET_RATE:
		if (host->RequestState == CMD_SEND) {
			rate = audio.format.sample_rate_hz;
			audio.rate[0] = (uint8_t)rate;
			audio.rate[1] = (uint8_t)(rate >> 8);
			audio.rate[2] = (uint8_t)(rate >> 16);
			/* Class request, endpoint recipient, host to device. */
			rate_request(host, 0x22U, AUDIO_REQ_SET_CUR);
		}
		result = USBH_CtlReq(host, audio.rate, 3U);
		if (result == USBH_OK) {
			audio.request = AUDIO_REQ_STEP_GET_RATE;
		} else if (result != USBH_BUSY) {
			printf("ST USBH: audio SET_CUR sampling rate failed: %u\n", result);
			audio_set_ready(false, "set sample rate failed");
			return USBH_FAIL;
		}
		return USBH_BUSY;

	case AUDIO_REQ_STEP_GET_RATE:
		if (host->RequestState == CMD_SEND) {
			memset(audio.rate, 0, sizeof(audio.rate));
			rate_request(host, 0xa2U, AUDIO_REQ_GET_CUR);
		}
		result = USBH_CtlReq(host, audio.rate, 3U);
		if (result == USBH_OK) {
			rate = audio.rate[0] | (audio.rate[1] << 8) | (audio.rate[2] << 16);
			printf("ST USBH: audio sampling rate readback %u Hz\n", rate);
			if (rate != audio.format.sample_rate_hz) {
				audio_set_ready(false, "sample rate mismatch");
				return USBH_FAIL;
			}
			audio.request = AUDIO_REQ_STEP_DONE;
		} else if (result == USBH_NOT_SUPPORTED) {
			/* GET_CUR is optional; SET_CUR was accepted. */
			printf("ST USBH: audio GET_CUR sampling rate not supported\n");
			audio.request = AUDIO_REQ_STEP_DONE;
		} else if (result != USBH_BUSY) {
			printf("ST USBH: audio GET_CUR sampling rate failed: %u\n", result);
			audio_set_ready(false, "get sample rate failed");
			return USBH_FAIL;
		}
		return USBH_BUSY;

	case AUDIO_REQ_STEP_DONE:
	default:
		audio_set_ready(true, "streaming");
		printf("ST USBH: audio capture ready on EP 0x%02x\n", audio.format.endpoint);
		return USBH_OK;
	}
}

static void audio_fail(USBH_HandleTypeDef *host, const char *reason)
{
	printf("ST USBH: audio capture stopped: %s\n", reason);
	audio_set_ready(false, reason);
	(void)USBH_ClosePipe(host, audio.pipe);
}

static bool audio_too_many_errors(void)
{
	return audio.consecutive_errors > AUDIO_MAX_CONSECUTIVE_ERRORS;
}

static void audio_consecutive_error(void)
{
	++audio.consecutive_errors;
}

static void audio_count_error(void)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	usb_audio_ring_note_error(&audio_ring);
	k_spin_unlock(&audio_lock, key);
	audio_consecutive_error();
}

static void audio_count_missed(void)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);

	usb_audio_ring_note_missed(&audio_ring);
	k_spin_unlock(&audio_lock, key);
}

static void audio_complete(USBH_HandleTypeDef *host)
{
	uint32_t length = USBH_LL_GetLastXferSize(host, audio.pipe);

	if (length > audio.format.packet_bytes) {
		audio_count_error();
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	struct usb_audio_snapshot before;
	struct usb_audio_snapshot after;

	usb_audio_ring_snapshot(&audio_ring, &before);
	(void)usb_audio_ring_write(&audio_ring, audio.packet, length);
	usb_audio_ring_snapshot(&audio_ring, &after);
	k_spin_unlock(&audio_lock, key);
	if (after.errors != before.errors) {
		/* The ring already counted the rejected packet. */
		audio_consecutive_error();
	} else {
		audio.consecutive_errors = 0U;
	}
}

static bool audio_submit(USBH_HandleTypeDef *host)
{
	return USBH_LL_SubmitURB(host, audio.pipe, 1U, USBH_EP_ISO,
				 USBH_PID_DATA, audio.packet,
				 audio.format.packet_bytes, 0U) == USBH_OK;
}

static uint32_t audio_reserve_submit_locked(USBH_HandleTypeDef *host)
{
	audio.submit_timer = host->Timer;
	audio.pending = true;
	return ++audio.generation;
}

static const char *audio_submit_reserved(USBH_HandleTypeDef *host,
					 uint32_t generation)
{
	unsigned int key = irq_lock();
	bool failed;
	bool submitted;

	if (audio.generation != generation || !audio.pending) {
		irq_unlock(key);
		return NULL;
	}
	submitted = audio_submit(host);
	failed = !submitted && audio.generation == generation && audio.pending;
	if (failed) {
		audio.pending = false;
		audio.failed = true;
		++audio.generation;
	}
	irq_unlock(key);
	return failed ? "iso submit failed" : NULL;
}

/*
 * HCD interrupt: an iso IN must be resubmitted within the 1 ms frame that
 * completed it. Doing it here instead of in the lowest-priority, busy-polling
 * USB thread keeps capture lossless while network threads (camera and
 * microphone streams) hold the CPU for more than a frame. Anything other
 * than a clean completion is left to audio_process().
 */
void usb_audio_capture_urb_done(USBH_HandleTypeDef *host, uint8_t pipe)
{
	if (!audio.allocated || audio.failed || audio.halting || !audio.pending ||
	    pipe != audio.pipe) {
		return;
	}
	audio.pending = false;
	++audio.generation;
	audio_complete(host);
	if (!audio_too_many_errors()) {
		/* Reserve state before starting hardware; IRQ is already in progress. */
		audio.submit_timer = host->Timer;
		audio.pending = true;
		++audio.generation;
		if (!audio_submit(host)) {
			audio.pending = false;
			++audio.generation;
		}
	}
}

static const char *audio_service(USBH_HandleTypeDef *host, bool *close_pipe)
{
	bool pending;
	bool halting;
	bool active = false;
	bool complete = false;
	uint8_t pipe;
	uint32_t generation;
	uint32_t age;
	USBH_URBStateTypeDef urb_state = USBH_URB_IDLE;
	unsigned int key = irq_lock();

	if (audio.failed || !audio.allocated) {
		irq_unlock(key);
		return NULL;
	}
	if (audio_too_many_errors()) {
		audio.failed = true;
		irq_unlock(key);
		return "isochronous IN errors";
	}

	pending = audio.pending;
	if (!pending) {
		generation = audio_reserve_submit_locked(host);
		irq_unlock(key);
		return audio_submit_reserved(host, generation);
	}

	pipe = audio.pipe;
	halting = audio.halting;
	generation = audio.generation;
	age = host->Timer - audio.submit_timer;
	irq_unlock(key);

	/*
	 * These HAL/register reads are outside irq_lock(). A completion may
	 * race them; generation below rejects a stale observation.
	 */
	active = stm32_hcd_adapter_channel_active(pipe);
	if (!halting) {
		urb_state = USBH_LL_GetURBState(host, pipe);
	}

	key = irq_lock();
	if (!audio.allocated || audio.failed || !audio.pending ||
	    audio.pipe != pipe || audio.generation != generation) {
		irq_unlock(key);
		return NULL;
	}

	if (halting) {
		if (active) {
			if (age > AUDIO_URB_HALT_FRAMES * 4U) {
				audio.failed = true;
				irq_unlock(key);
				return "iso channel did not halt";
			}
			irq_unlock(key);
			return NULL;
		}
		audio.halting = false;
		audio.pending = false;
		++audio.generation;
	} else {
		switch (urb_state) {
		case USBH_URB_DONE:
			audio.pending = false;
			++audio.generation;
			complete = true;
			break;
		case USBH_URB_NOTREADY:
		case USBH_URB_ERROR:
		case USBH_URB_STALL:
			audio.pending = false;
			++audio.generation;
			audio_count_error();
			break;
		default:
			/*
			 * HAL halts on frame overrun without changing
			 * the URB state: a disabled channel that never
			 * completed is a missed frame.
			 */
			if (age >= AUDIO_URB_MISSED_FRAMES && !active &&
			    urb_state == USBH_URB_IDLE) {
				audio.pending = false;
				++audio.generation;
				audio_count_missed();
			} else if (age >= AUDIO_URB_HALT_FRAMES) {
				audio_count_missed();
				audio.halting = true;
				*close_pipe = true;
			}
			break;
		}
	}

	if (audio_too_many_errors()) {
		audio.failed = true;
		irq_unlock(key);
		if (complete) {
			audio_complete(host);
		}
		return "isochronous IN errors";
	}
	pending = audio.pending;
	if (!pending && !complete) {
		generation = audio_reserve_submit_locked(host);
	}
	irq_unlock(key);

	if (complete) {
		audio_complete(host);
		key = irq_lock();
		if (!audio.allocated || audio.failed || audio.pending) {
			irq_unlock(key);
			return NULL;
		}
		if (audio_too_many_errors()) {
			audio.failed = true;
			irq_unlock(key);
			return "isochronous IN errors";
		}
		generation = audio_reserve_submit_locked(host);
		irq_unlock(key);
		return audio_submit_reserved(host, generation);
	}
	if (*close_pipe) {
		return NULL;
	}
	return pending ? NULL : audio_submit_reserved(host, generation);
}

static USBH_StatusTypeDef audio_process(USBH_HandleTypeDef *host)
{
	bool close_pipe = false;
	const char *failure = audio_service(host, &close_pipe);

	/* Do teardown and logging only after audio_service() releases its lock. */
	if (failure != NULL) {
		audio_fail(host, failure);
	} else if (close_pipe) {
		(void)USBH_ClosePipe(host, audio.pipe);
	}
	return USBH_OK;
}

static USBH_StatusTypeDef audio_sof(USBH_HandleTypeDef *host)
{
	ARG_UNUSED(host);
	return USBH_OK;
}

USBH_ClassTypeDef usb_audio_capture_class = {
	.Name = "UAC1 microphone",
	.ClassCode = AUDIO_CLASS,
	.Init = audio_init,
	.DeInit = audio_deinit,
	.Requests = audio_requests,
	.BgndProcess = audio_process,
	.SOFProcess = audio_sof,
};
