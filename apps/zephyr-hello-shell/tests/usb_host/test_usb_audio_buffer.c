/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_audio_buffer.h"

#define RATE 48000U
#define FRAME 4U /* stereo, 16-bit */
#define PACKET (48U * FRAME)

static struct usb_audio_ring ring;

/* Fill a 1 ms packet whose frames carry an increasing sequence number. */
static void make_packet(uint8_t *pkt, uint16_t first_frame, int16_t sample)
{
	for (uint16_t i = 0; i < PACKET / FRAME; i++) {
		uint16_t seq = (uint16_t)(first_frame + i);
		int16_t right = sample;

		pkt[i * FRAME + 0] = (uint8_t)(seq & 0xffU);
		pkt[i * FRAME + 1] = (uint8_t)(seq >> 8);
		pkt[i * FRAME + 2] = (uint8_t)((uint16_t)right & 0xffU);
		pkt[i * FRAME + 3] = (uint8_t)((uint16_t)right >> 8);
	}
}

static uint16_t frame_seq(const uint8_t *buf, size_t frame)
{
	return (uint16_t)(buf[frame * FRAME] | (buf[frame * FRAME + 1] << 8));
}

static uint32_t start(void)
{
	usb_audio_ring_init(&ring, "no audio device");
	uint32_t session = usb_audio_ring_attach(&ring, RATE, 2U, 16U);

	assert(session != 0U);
	usb_audio_ring_set_ready(&ring, true, "streaming");
	return session;
}

static void test_initial_state(void)
{
	struct usb_audio_snapshot snap;
	uint8_t out[64];

	usb_audio_ring_init(&ring, "no audio device");
	usb_audio_ring_snapshot(&ring, &snap);
	assert(!snap.connected && !snap.ready);
	assert(snap.packets == 0U && snap.drops == 0U && snap.errors == 0U);
	assert(strcmp(snap.reason, "no audio device") == 0);
	assert(usb_audio_ring_read(&ring, snap.session, out, sizeof(out)) == 0U);
	puts("PASS: initial state");
}

static void test_fifo_order_and_frames(void)
{
	uint8_t pkt[PACKET];
	uint8_t out[PACKET * 3];
	uint32_t session = start();
	struct usb_audio_snapshot snap;

	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.connected && snap.ready && snap.session == session);
	assert(snap.sample_rate_hz == RATE && snap.channels == 2U);
	assert(snap.bits_per_sample == 16U);

	/* Empty reads return zero. */
	assert(usb_audio_ring_read(&ring, session, out, sizeof(out)) == 0U);

	for (uint16_t p = 0; p < 3; p++) {
		make_packet(pkt, (uint16_t)(p * 48U), 0);
		assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	}
	/* Capacity that is not a whole frame count rounds down. */
	size_t n = usb_audio_ring_read(&ring, session, out, 10U);

	assert(n == 8U);
	assert(frame_seq(out, 0) == 0U && frame_seq(out, 1) == 1U);
	n = usb_audio_ring_read(&ring, session, out, sizeof(out));
	assert(n == 3U * PACKET - 8U);
	for (size_t f = 0; f < n / FRAME; f++) {
		assert(frame_seq(out, f) == (uint16_t)(f + 2U));
	}
	assert(usb_audio_ring_read(&ring, session, out, sizeof(out)) == 0U);
	/* Too-small reader buffer gets nothing rather than a partial frame. */
	assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	assert(usb_audio_ring_read(&ring, session, out, FRAME - 1U) == 0U);

	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.packets == 4U && snap.drops == 0U && snap.errors == 0U);
	puts("PASS: FIFO order, complete frames, empty reads");
}

static void test_level(void)
{
	uint8_t pkt[PACKET];
	struct usb_audio_snapshot snap;

	start();
	memset(pkt, 0, sizeof(pkt));
	assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.level == 0U);

	/* Left channel zero, right channel -12000: peak magnitude is 12000. */
	for (size_t i = 0; i < PACKET / FRAME; i++) {
		int16_t v = (i == 7U) ? -12000 : 100;

		pkt[i * FRAME + 2] = (uint8_t)((uint16_t)v & 0xffU);
		pkt[i * FRAME + 3] = (uint8_t)((uint16_t)v >> 8);
	}
	assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.level == 12000U);

	/* Silence decays the level all the way back to zero. */
	memset(pkt, 0, sizeof(pkt));
	for (int i = 0; i < 200; i++) {
		assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	}
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.level == 0U);

	/* INT16_MIN must not overflow the magnitude. */
	pkt[2] = 0x00;
	pkt[3] = 0x80;
	assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.level == 32767U);
	puts("PASS: level from samples");
}

static void test_overflow_drops_oldest(void)
{
	static uint8_t out[USB_AUDIO_RING_BYTES + PACKET];
	uint8_t pkt[PACKET];
	uint32_t session = start();
	struct usb_audio_snapshot snap;
	size_t capacity_frames = usb_audio_ring_capacity(&ring) / FRAME;
	size_t packets = capacity_frames / 48U + 3U;

	assert(usb_audio_ring_capacity(&ring) % FRAME == 0U);
	assert(capacity_frames >= RATE / 1000U * 200U); /* >= 200 ms */
	for (size_t p = 0; p < packets; p++) {
		make_packet(pkt, (uint16_t)(p * 48U), 0);
		assert(usb_audio_ring_write(&ring, pkt, sizeof(pkt)) == sizeof(pkt));
	}
	size_t written_frames = packets * 48U;
	size_t dropped = written_frames - capacity_frames;

	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.drops == dropped);
	assert(snap.packets == packets);
	size_t n = usb_audio_ring_read(&ring, session, out, sizeof(out));

	assert(n == capacity_frames * FRAME);
	assert(frame_seq(out, 0) == (uint16_t)dropped);
	assert(frame_seq(out, capacity_frames - 1U) ==
	       (uint16_t)(written_frames - 1U));
	puts("PASS: overflow drops oldest complete frames");
}

static void test_invalid_packets(void)
{
	uint8_t pkt[PACKET];
	uint8_t out[PACKET];
	uint32_t session = start();
	struct usb_audio_snapshot snap;

	make_packet(pkt, 0, 0);
	/* A packet that is not whole frames is rejected and counted. */
	assert(usb_audio_ring_write(&ring, pkt, PACKET - 1U) == 0U);
	assert(usb_audio_ring_write(&ring, NULL, PACKET) == 0U);
	/* Zero-length packet is a valid empty iso packet. */
	assert(usb_audio_ring_write(&ring, pkt, 0U) == 0U);
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.errors == 2U && snap.packets == 1U);
	assert(usb_audio_ring_read(&ring, session, out, sizeof(out)) == 0U);

	/* Before ready, nothing is accepted (no counterfeit data). */
	usb_audio_ring_set_ready(&ring, false, "configuring");
	assert(usb_audio_ring_write(&ring, pkt, PACKET) == 0U);
	assert(usb_audio_ring_read(&ring, session, out, sizeof(out)) == 0U);
	usb_audio_ring_note_error(&ring);
	usb_audio_ring_note_missed(&ring);
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.errors == 3U && snap.missed == 1U);
	assert(strcmp(snap.reason, "configuring") == 0);
	puts("PASS: invalid packets rejected");
}

static void test_disconnect_invalidates_session(void)
{
	uint8_t pkt[PACKET];
	uint8_t out[PACKET];
	uint32_t old = start();
	struct usb_audio_snapshot snap;

	make_packet(pkt, 0, 1000);
	assert(usb_audio_ring_write(&ring, pkt, PACKET) == PACKET);
	/* Short read keeps counters. */
	assert(usb_audio_ring_read(&ring, old, out, FRAME) == FRAME);
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.packets == 1U && snap.level == 1000U);

	usb_audio_ring_detach(&ring, "disconnected");
	usb_audio_ring_snapshot(&ring, &snap);
	assert(!snap.connected && !snap.ready);
	assert(snap.session == old + 1U);
	assert(snap.level == 0U);
	assert(strcmp(snap.reason, "disconnected") == 0);
	assert(usb_audio_ring_read(&ring, old, out, sizeof(out)) == 0U);
	assert(usb_audio_ring_read(&ring, snap.session, out, sizeof(out)) == 0U);
	assert(usb_audio_ring_write(&ring, pkt, PACKET) == 0U);

	/* Re-attach: new session, stale bytes gone, counters restart. */
	uint32_t fresh = usb_audio_ring_attach(&ring, RATE, 2U, 16U);

	assert(fresh == old + 2U);
	usb_audio_ring_set_ready(&ring, true, "streaming");
	assert(usb_audio_ring_read(&ring, old, out, sizeof(out)) == 0U);
	assert(usb_audio_ring_read(&ring, fresh, out, sizeof(out)) == 0U);
	usb_audio_ring_snapshot(&ring, &snap);
	assert(snap.packets == 0U && snap.drops == 0U && snap.errors == 0U);
	make_packet(pkt, 500, 0);
	assert(usb_audio_ring_write(&ring, pkt, PACKET) == PACKET);
	assert(usb_audio_ring_read(&ring, old, out, sizeof(out)) == 0U);
	assert(usb_audio_ring_read(&ring, fresh, out, sizeof(out)) == PACKET);
	assert(frame_seq(out, 0) == 500U);
	puts("PASS: disconnect invalidates session");
}

static void test_bad_formats(void)
{
	usb_audio_ring_init(&ring, "no audio device");
	assert(usb_audio_ring_attach(&ring, RATE, 0U, 16U) == 0U);
	assert(usb_audio_ring_attach(&ring, RATE, 2U, 12U) == 0U);
	assert(usb_audio_ring_attach(&ring, 0U, 2U, 16U) == 0U);
	struct usb_audio_snapshot snap;

	usb_audio_ring_snapshot(&ring, &snap);
	assert(!snap.connected);

	/* 24-bit stereo uses 6-byte frames and a frame-aligned capacity. */
	assert(usb_audio_ring_attach(&ring, RATE, 2U, 24U) != 0U);
	assert(usb_audio_ring_capacity(&ring) % 6U == 0U);
	puts("PASS: format validation");
}

int main(void)
{
	test_initial_state();
	test_fifo_order_and_frames();
	test_level();
	test_overflow_drops_oldest();
	test_invalid_packets();
	test_disconnect_invalidates_session();
	test_bad_formats();
	puts("ALL TESTS PASSED");
	return 0;
}
