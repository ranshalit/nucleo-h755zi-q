/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usb_audio_desc.h"

#define DEFAULT_FIXTURE \
	"apps/zephyr-hello-shell/tests/usb_host/fixtures/h570e_config.hex"
#define MAX_FIXTURE 1024

static const char *fixture_path = DEFAULT_FIXTURE;
static uint8_t fixture[MAX_FIXTURE];
static size_t fixture_len;

static void load_fixture(void)
{
	FILE *f = fopen(fixture_path, "r");
	unsigned int byte;

	if (f == NULL) {
		fprintf(stderr, "cannot open fixture %s\n", fixture_path);
		exit(1);
	}
	fixture_len = 0;
	while (fscanf(f, "%x", &byte) == 1) {
		assert(fixture_len < MAX_FIXTURE);
		assert(byte <= 0xff);
		fixture[fixture_len++] = (uint8_t)byte;
	}
	fclose(f);
	assert(fixture_len == 291);
}

/* Copy of the fixture that individual tests may mutate. */
static uint8_t work[MAX_FIXTURE];

static void reset_work(void)
{
	memcpy(work, fixture, fixture_len);
}

static size_t find(const uint8_t *buf, size_t len, const uint8_t *pat,
		   size_t pat_len)
{
	for (size_t i = 0; i + pat_len <= len; i++) {
		if (memcmp(&buf[i], pat, pat_len) == 0) {
			return i;
		}
	}
	fprintf(stderr, "pattern not found\n");
	exit(1);
}

/* Offset of the mic endpoint descriptor: 09 05 81 05 ... */
static size_t mic_ep_offset(void)
{
	static const uint8_t pat[] = { 0x09, 0x05, 0x81, 0x05 };

	return find(work, fixture_len, pat, sizeof(pat));
}

/* Offset of IF1 AS_GENERAL: 07 24 01 03 (terminal link 3) */
static size_t mic_as_general_offset(void)
{
	static const uint8_t pat[] = { 0x07, 0x24, 0x01, 0x03 };

	return find(work, fixture_len, pat, sizeof(pat));
}

static void expect_fail(size_t len, int expected)
{
	struct usb_audio_format out;
	struct usb_audio_format sentinel;

	memset(&out, 0xa5, sizeof(out));
	memcpy(&sentinel, &out, sizeof(out));
	int rc = usb_audio_select_input(work, len, &out);

	if (rc != expected) {
		fprintf(stderr, "expected %d got %d\n", expected, rc);
	}
	assert(rc == expected);
	assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
}

static void test_fixture_selects_mic_input(void)
{
	struct usb_audio_format out;
	static const uint8_t advertised_alts[][2] = { { 1, 1 } };
	int found_alt = 0;

	reset_work();
	assert(usb_audio_select_input(work, fixture_len, &out) == 0);

	assert(out.endpoint & 0x80);
	assert(out.endpoint == 0x81);
	for (size_t i = 0; i < sizeof(advertised_alts) / 2; i++) {
		if (out.interface_number == advertised_alts[i][0] &&
		    out.alternate_setting == advertised_alts[i][1]) {
			found_alt = 1;
		}
	}
	assert(found_alt);

	/* Endpoint is isochronous in the raw descriptor. */
	size_t ep = mic_ep_offset();

	assert(work[ep + 2] == out.endpoint);
	assert((work[ep + 3] & 0x03) == 0x01);

	assert(out.sample_rate_hz == 48000);
	assert(out.channels == 2);
	assert(out.bits_per_sample == 16);
	assert(out.interval == 1);
	assert(out.packet_bytes == 200);
	assert(out.packet_bytes >=
	       (out.sample_rate_hz * out.channels * 2 + 999) / 1000);
	assert(out.audio_class_bcd == 0x0100);
	printf("PASS: test_fixture_selects_mic_input\n");
}

static void test_null_arguments(void)
{
	struct usb_audio_format out;

	reset_work();
	assert(usb_audio_select_input(NULL, fixture_len, &out) == -EINVAL);
	assert(usb_audio_select_input(work, fixture_len, NULL) == -EINVAL);
	expect_fail(0, -EINVAL);
	expect_fail(8, -EINVAL);
	printf("PASS: test_null_arguments\n");
}

static void test_zero_blength(void)
{
	reset_work();
	work[9] = 0; /* first descriptor after the config header */
	expect_fail(fixture_len, -EINVAL);

	reset_work();
	work[mic_ep_offset()] = 0;
	expect_fail(fixture_len, -EINVAL);

	reset_work();
	work[mic_ep_offset()] = 1;
	expect_fail(fixture_len, -EINVAL);
	printf("PASS: test_zero_blength\n");
}

static void test_descriptor_past_total_length(void)
{
	/* wTotalLength = 20 cuts the 10-byte AC header at offset 18. */
	reset_work();
	work[2] = 20;
	work[3] = 0;
	expect_fail(fixture_len, -EINVAL);

	/* Last descriptor grows past wTotalLength. */
	reset_work();
	work[fixture_len - 7] = 9;
	expect_fail(fixture_len, -EINVAL);

	/* Buffer shorter than declared wTotalLength. */
	reset_work();
	expect_fail(fixture_len - 1, -EINVAL);

	/* Not a configuration descriptor. */
	reset_work();
	work[1] = 0x01;
	expect_fail(fixture_len, -EINVAL);
	printf("PASS: test_descriptor_past_total_length\n");
}

static void test_no_audio_in_endpoint(void)
{
	/* Mic endpoint turned into OUT: only speaker-like streams remain. */
	reset_work();
	work[mic_ep_offset() + 2] = 0x03;
	expect_fail(fixture_len, -ENOTSUP);

	/* Mic endpoint turned bulk. */
	reset_work();
	work[mic_ep_offset() + 3] = 0x02;
	expect_fail(fixture_len, -ENOTSUP);
	printf("PASS: test_no_audio_in_endpoint\n");
}

static void test_non_pcm_format(void)
{
	size_t g;

	/* wFormatTag = 0x0003 (IEEE float). */
	reset_work();
	g = mic_as_general_offset();
	work[g + 5] = 0x03;
	work[g + 6] = 0x00;
	expect_fail(fixture_len, -ENOTSUP);

	/* Format type descriptor claims Type II. */
	reset_work();
	g = mic_as_general_offset();
	assert(work[g + 7] == 0x1a && work[g + 9] == 0x02);
	work[g + 10] = 0x02;
	expect_fail(fixture_len, -ENOTSUP);
	printf("PASS: test_non_pcm_format\n");
}

static void test_packet_capacity_too_small(void)
{
	size_t ep;
	struct usb_audio_format out;

	/* 31 bytes < ceil(8000 * 2 * 2 / 1000) = 32: no rate fits. */
	reset_work();
	ep = mic_ep_offset();
	work[ep + 4] = 31;
	work[ep + 5] = 0;
	expect_fail(fixture_len, -ENOTSUP);

	/* 191 bytes < 192 for 48 kHz; 44.1 kHz needs 177 and fits. */
	reset_work();
	ep = mic_ep_offset();
	work[ep + 4] = 191;
	work[ep + 5] = 0;
	assert(usb_audio_select_input(work, fixture_len, &out) == 0);
	assert(out.sample_rate_hz == 44100);
	assert(out.packet_bytes == 191);
	printf("PASS: test_packet_capacity_too_small\n");
}

static void test_truncated_format_descriptor(void)
{
	size_t g;

	/* Format type descriptor claims more discrete rates than fit. */
	reset_work();
	g = mic_as_general_offset();
	work[g + 14] = 7;
	expect_fail(fixture_len, -EINVAL);
	printf("PASS: test_truncated_format_descriptor\n");
}

static void test_uac2_protocol_not_supported(void)
{
	static const uint8_t pat[] = { 0x09, 0x04, 0x01, 0x01, 0x01,
				       0x01, 0x02, 0x00 };
	size_t i;

	reset_work();
	i = find(work, fixture_len, pat, sizeof(pat));
	work[i + 7] = 0x20; /* bInterfaceProtocol IP_VERSION_02_00 */
	expect_fail(fixture_len, -ENOTSUP);
	printf("PASS: test_uac2_protocol_not_supported\n");
}

static void test_link_must_be_usb_streaming_output(void)
{
	/* Link the mic stream to terminal 4, a USB-streaming *input*. */
	reset_work();
	work[mic_as_general_offset() + 3] = 0x04;
	expect_fail(fixture_len, -ENOTSUP);
	printf("PASS: test_link_must_be_usb_streaming_output\n");
}

static void test_continuous_rate_range(void)
{
	struct usb_audio_format out;

	/* bSamFreqType = 0: range [8000, 16000] from the first two entries. */
	reset_work();
	work[mic_as_general_offset() + 14] = 0;
	assert(usb_audio_select_input(work, fixture_len, &out) == 0);
	assert(out.sample_rate_hz == 16000);
	printf("PASS: test_continuous_rate_range\n");
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		fixture_path = argv[1];
	}
	load_fixture();
	test_fixture_selects_mic_input();
	test_null_arguments();
	test_zero_blength();
	test_descriptor_past_total_length();
	test_no_audio_in_endpoint();
	test_non_pcm_format();
	test_packet_capacity_too_small();
	test_truncated_format_descriptor();
	test_uac2_protocol_not_supported();
	test_link_must_be_usb_streaming_output();
	test_continuous_rate_range();
	printf("ALL TESTS PASSED\n");
	return 0;
}
