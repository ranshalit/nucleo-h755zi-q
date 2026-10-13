/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_audio_desc.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define DESC_CONFIGURATION 0x02
#define DESC_INTERFACE 0x04
#define DESC_ENDPOINT 0x05
#define DESC_CS_INTERFACE 0x24

#define USB_CLASS_AUDIO 0x01
#define AUDIO_SUBCLASS_CONTROL 0x01
#define AUDIO_SUBCLASS_STREAMING 0x02
#define AUDIO_PROTOCOL_UAC1 0x00
#define UAC1_BCD 0x0100

#define AC_HEADER 0x01
#define AC_OUTPUT_TERMINAL 0x03
#define AS_GENERAL 0x01
#define AS_FORMAT_TYPE 0x02

#define TERMINAL_USB_STREAMING 0x0101
#define FORMAT_TAG_PCM 0x0001
#define FORMAT_TYPE_I 0x01

#define EP_DIR_IN 0x80
#define EP_XFER_MASK 0x03
#define EP_XFER_ISOC 0x01
#define EP_USAGE_MASK 0x30
#define EP_USAGE_DATA 0x00
#define EP_ADDRESS_RESERVED_MASK 0x70
#define EP_NUMBER_MASK 0x0f
#define EP_MPS_SIZE_MASK 0x07ff
#define EP_MPS_EXTRA_MASK 0x1800
#define EP_MPS_RESERVED_MASK 0xe000

#define FS_FRAMES_PER_SECOND 1000U
#define MAX_CHANNELS 8

/* Streaming alternate setting accumulated while walking descriptors. */
struct as_alt {
	bool active;
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t protocol;
	uint16_t audio_class_bcd;
	bool link_is_usb_output;

	bool have_general;
	uint16_t format_tag;

	bool have_format;
	uint8_t format_type;
	uint8_t channels;
	uint8_t subframe_bytes;
	uint8_t bit_resolution;
	uint8_t rate_count; /* 0 = continuous [rates[0], rates[1]] */
	const uint8_t *rates;

	bool have_endpoint;
	uint8_t endpoint;
	uint8_t ep_attributes;
	uint16_t ep_max_packet;
	uint8_t ep_interval;
};

struct audio_function {
	uint16_t bcd;
	uint8_t usb_output_terminals[32]; /* bitmap by terminal ID */
	uint8_t collection_interfaces[32];
	uint8_t streaming_interfaces[32];
	uint16_t ac_total_declared;
	size_t ac_total_observed;
	bool ac_header_seen;
};

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_le24(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16);
}

static uint32_t frame_bytes(uint32_t rate, const struct as_alt *a)
{
	uint32_t samples_per_frame =
		(rate + FS_FRAMES_PER_SECOND - 1U) / FS_FRAMES_PER_SECOND;

	return samples_per_frame * a->channels * a->subframe_bytes;
}

static bool rate_fits(uint32_t rate, const struct as_alt *a, uint16_t mps)
{
	return rate != 0U && frame_bytes(rate, a) <= mps;
}

/* Higher score is better: preferred rate first, then highest rate. */
static uint64_t rate_score(uint32_t rate)
{
	return ((uint64_t)(rate == USB_AUDIO_PREFERRED_RATE_HZ) << 32) | rate;
}

/* Returns the best usable rate for this alternate, or 0 if none fits. */
static uint32_t pick_rate(const struct as_alt *a, uint16_t mps)
{
	uint32_t best = 0;

	if (a->rate_count == 0U) {
		uint32_t lo = get_le24(&a->rates[0]);
		uint32_t hi = get_le24(&a->rates[3]);
		uint32_t pref = USB_AUDIO_PREFERRED_RATE_HZ;

		if (lo > hi) {
			return 0;
		}
		if (pref >= lo && pref <= hi && rate_fits(pref, a, mps)) {
			return pref;
		}
		/* Largest rate whose 1 ms payload fits in the packet. */
		uint32_t bytes_per_sample =
			(uint32_t)a->channels * a->subframe_bytes;
		uint32_t max_rate =
			((uint32_t)mps / bytes_per_sample) *
			FS_FRAMES_PER_SECOND;

		best = max_rate < hi ? max_rate : hi;
		return best >= lo && rate_fits(best, a, mps) ? best : 0;
	}

	for (uint8_t i = 0; i < a->rate_count; i++) {
		uint32_t rate = get_le24(&a->rates[3U * i]);

		if (rate_fits(rate, a, mps) &&
		    rate_score(rate) > rate_score(best)) {
			best = rate;
		}
	}
	return best;
}

/* Fills *fmt and returns true when the alternate is a usable mic input. */
static bool evaluate_alt(const struct as_alt *a, struct usb_audio_format *fmt)
{
	if (!a->active || a->alternate_setting == 0U ||
	    a->protocol != AUDIO_PROTOCOL_UAC1 ||
	    a->audio_class_bcd != UAC1_BCD || !a->link_is_usb_output ||
	    !a->have_general || !a->have_format || !a->have_endpoint) {
		return false;
	}
	if (a->format_tag != FORMAT_TAG_PCM ||
	    a->format_type != FORMAT_TYPE_I) {
		return false;
	}
	/* Integer PCM with no padding bits so consumers know sample size. */
	if (a->channels == 0U || a->channels > MAX_CHANNELS ||
	    a->subframe_bytes < 2U || a->subframe_bytes > 4U ||
	    a->bit_resolution != a->subframe_bytes * 8U) {
		return false;
	}
	if ((a->endpoint & EP_DIR_IN) == 0U ||
	    (a->ep_attributes & EP_XFER_MASK) != EP_XFER_ISOC ||
	    (a->ep_attributes & EP_USAGE_MASK) != EP_USAGE_DATA) {
		return false;
	}
	/* Full-speed only: one packet per 1 ms frame. */
	if ((a->ep_max_packet & EP_MPS_EXTRA_MASK) != 0U ||
	    a->ep_interval != 1U) {
		return false;
	}

	uint16_t mps = a->ep_max_packet & EP_MPS_SIZE_MASK;
	uint32_t rate = pick_rate(a, mps);

	if (rate == 0U) {
		return false;
	}

	fmt->interface_number = a->interface_number;
	fmt->alternate_setting = a->alternate_setting;
	fmt->endpoint = a->endpoint;
	fmt->channels = a->channels;
	fmt->bits_per_sample = a->bit_resolution;
	fmt->interval = a->ep_interval;
	fmt->packet_bytes = mps;
	fmt->sample_rate_hz = rate;
	fmt->audio_class_bcd = a->audio_class_bcd;
	return true;
}

static void finish_alt(struct as_alt *a, struct usb_audio_format *best,
		       bool *have_best)
{
	struct usb_audio_format fmt;

	if (evaluate_alt(a, &fmt) &&
	    (!*have_best ||
	     rate_score(fmt.sample_rate_hz) > rate_score(best->sample_rate_hz))) {
		*best = fmt;
		*have_best = true;
	}
	memset(a, 0, sizeof(*a));
}

static int parse_ac(const uint8_t *d, struct audio_function *fn)
{
	uint8_t len = d[0];

	if (len < 3U) {
		return -EINVAL;
	}
	if (d[2] == AC_HEADER) {
		if (fn->ac_header_seen || len < 8U ||
		    len != 8U + d[7]) {
			return -EINVAL;
		}
		fn->ac_header_seen = true;
		fn->bcd = get_le16(&d[3]);
		fn->ac_total_declared = get_le16(&d[5]);
		fn->ac_total_observed = len;
		if (fn->ac_total_declared < len) {
			return -EINVAL;
		}
		for (uint8_t i = 0; i < d[7]; i++) {
			uint8_t interface_number = d[8U + i];
			uint8_t mask = (uint8_t)(1U << (interface_number % 8U));
			uint8_t *slot =
				&fn->collection_interfaces[interface_number / 8U];

			if ((*slot & mask) != 0U) {
				return -EINVAL;
			}
			*slot |= mask;
		}
		return 0;
	}
	if (!fn->ac_header_seen ||
	    fn->ac_total_observed + len > fn->ac_total_declared) {
		return -EINVAL;
	}
	fn->ac_total_observed += len;

	switch (d[2]) {
	case AC_OUTPUT_TERMINAL:
		if (len < 9U) {
			return -EINVAL;
		}
		if (get_le16(&d[4]) == TERMINAL_USB_STREAMING) {
			fn->usb_output_terminals[d[3] / 8U] |=
				(uint8_t)(1U << (d[3] % 8U));
		}
		break;
	default:
		if (len < 3U) {
			return -EINVAL;
		}
		break;
	}
	return 0;
}

static int finish_ac(const struct audio_function *fn)
{
	return fn->ac_header_seen &&
	       fn->ac_total_observed == fn->ac_total_declared ? 0 : -EINVAL;
}

static int validate_ac_interfaces(const struct audio_function *fn)
{
	if (!fn->ac_header_seen) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(fn->collection_interfaces); i++) {
		if (fn->collection_interfaces[i] != fn->streaming_interfaces[i]) {
			return -EINVAL;
		}
	}
	return 0;
}

static int parse_as(const uint8_t *d, struct as_alt *a,
		    const struct audio_function *fn)
{
	uint8_t len = d[0];

	if (len < 3U) {
		return -EINVAL;
	}
	if (d[2] == AS_GENERAL) {
		if (len < 7U) {
			return -EINVAL;
		}
		uint8_t link = d[3];

		a->have_general = true;
		a->format_tag = get_le16(&d[5]);
		a->link_is_usb_output =
			(fn->usb_output_terminals[link / 8U] &
			 (1U << (link % 8U))) != 0U;
	} else if (d[2] == AS_FORMAT_TYPE) {
		if (len < 4U) {
			return -EINVAL;
		}
		a->have_format = true;
		a->format_type = d[3];
		if (d[3] == 0x02U) {
			if (len < 9U) {
				return -EINVAL;
			}
			uint8_t n = d[8];
			size_t need = 9U + (n == 0U ? 6U : 3U * (size_t)n);

			if (len < need) {
				return -EINVAL;
			}
			return 0;
		}
		if (d[3] == FORMAT_TYPE_I || d[3] == 0x03U) {
			if (len < 8U) {
				return -EINVAL;
			}
			uint8_t n = d[7];
			size_t need = 8U + (n == 0U ? 6U : 3U * (size_t)n);

			if (len < need) {
				return -EINVAL;
			}
			if (d[3] != FORMAT_TYPE_I) {
				return 0;
			}
			a->channels = d[4];
			a->subframe_bytes = d[5];
			a->bit_resolution = d[6];
			a->rate_count = n;
			a->rates = &d[8];
		}
	}
	return 0;
}

int usb_audio_select_input(const uint8_t *raw, size_t length,
			   struct usb_audio_format *out)
{
	struct audio_function fn;
	struct as_alt alt;
	struct usb_audio_format best;
	bool have_best = false;
	uint8_t if_class = 0;
	uint8_t if_subclass = 0;
	int rc;

	if (raw == NULL || out == NULL || length < 9U) {
		return -EINVAL;
	}
	if (raw[0] < 9U || raw[1] != DESC_CONFIGURATION) {
		return -EINVAL;
	}

	size_t total = get_le16(&raw[2]);

	if (total < raw[0] || total > length) {
		return -EINVAL;
	}

	memset(&fn, 0, sizeof(fn));
	memset(&alt, 0, sizeof(alt));
	memset(&best, 0, sizeof(best));

	for (size_t off = raw[0]; off < total;) {
		const uint8_t *d = &raw[off];

		if (total - off < 2U || d[0] < 2U || d[0] > total - off) {
			return -EINVAL;
		}

		switch (d[1]) {
		case DESC_INTERFACE:
			if (d[0] < 9U) {
				return -EINVAL;
			}
			if (if_class == USB_CLASS_AUDIO &&
			    if_subclass == AUDIO_SUBCLASS_CONTROL) {
				rc = finish_ac(&fn);
				if (rc != 0) {
					return rc;
				}
			}
			finish_alt(&alt, &best, &have_best);
			if_class = d[5];
			if_subclass = d[6];
			if (if_class == USB_CLASS_AUDIO &&
			    if_subclass == AUDIO_SUBCLASS_STREAMING) {
				uint8_t *slot = &fn.streaming_interfaces[d[2] / 8U];

				*slot |= (uint8_t)(1U << (d[2] % 8U));
			}
			if (if_class == USB_CLASS_AUDIO &&
			    if_subclass == AUDIO_SUBCLASS_STREAMING) {
				alt.active = true;
				alt.interface_number = d[2];
				alt.alternate_setting = d[3];
				alt.protocol = d[7];
				alt.audio_class_bcd = fn.bcd;
			}
			break;
		case DESC_ENDPOINT:
			if (d[0] < 7U) {
				return -EINVAL;
			}
			if ((d[2] & EP_NUMBER_MASK) == 0U ||
			    (d[2] & EP_ADDRESS_RESERVED_MASK) != 0U ||
			    (get_le16(&d[4]) & EP_MPS_RESERVED_MASK) != 0U) {
				return -EINVAL;
			}
			/* First isochronous data endpoint carries the stream. */
			if (alt.active && !alt.have_endpoint &&
			    (d[3] & EP_USAGE_MASK) == EP_USAGE_DATA) {
				alt.have_endpoint = true;
				alt.endpoint = d[2];
				alt.ep_attributes = d[3];
				alt.ep_max_packet = get_le16(&d[4]);
				alt.ep_interval = d[6];
			}
			break;
		case DESC_CS_INTERFACE:
			if (if_class != USB_CLASS_AUDIO) {
				break;
			}
			if (if_subclass == AUDIO_SUBCLASS_CONTROL) {
				rc = parse_ac(d, &fn);
			} else if (alt.active) {
				rc = parse_as(d, &alt, &fn);
			} else {
				rc = 0;
			}
			if (rc != 0) {
				return rc;
			}
			break;
		default:
			break;
		}
		off += d[0];
	}
	if (if_class == USB_CLASS_AUDIO &&
	    if_subclass == AUDIO_SUBCLASS_CONTROL) {
		rc = finish_ac(&fn);
		if (rc != 0) {
			return rc;
		}
	}
	finish_alt(&alt, &best, &have_best);
	if (validate_ac_interfaces(&fn) != 0) {
		return -EINVAL;
	}

	if (!have_best) {
		return -ENOTSUP;
	}
	*out = best;
	return 0;
}
