/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "audio_wire.h"

int main(void)
{
	uint8_t header[16];
	const uint8_t expected[16] = {
		0x4d, 0x49, 0x43, 0x31, 0x80, 0x3e, 0, 0,
		1, 0, 16, 0, 0x78, 0x56, 0x34, 0x12,
	};

	assert(audio_wire_header(header, 16000, 1, 16, 0x12345678) == 0);
	assert(memcmp(header, expected, sizeof(header)) == 0);
	return 0;
}
