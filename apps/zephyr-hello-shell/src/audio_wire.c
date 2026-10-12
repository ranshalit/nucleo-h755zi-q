/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_wire.h"

#include <errno.h>
#include <stddef.h>

int audio_wire_header(uint8_t out[16], uint32_t rate, uint16_t channels,
		      uint16_t bits, uint32_t session)
{
	if (out == NULL) {
		return -EINVAL;
	}

	out[0] = 'M';
	out[1] = 'I';
	out[2] = 'C';
	out[3] = '1';
	out[4] = (uint8_t)rate;
	out[5] = (uint8_t)(rate >> 8);
	out[6] = (uint8_t)(rate >> 16);
	out[7] = (uint8_t)(rate >> 24);
	out[8] = (uint8_t)channels;
	out[9] = (uint8_t)(channels >> 8);
	out[10] = (uint8_t)bits;
	out[11] = (uint8_t)(bits >> 8);
	out[12] = (uint8_t)session;
	out[13] = (uint8_t)(session >> 8);
	out[14] = (uint8_t)(session >> 16);
	out[15] = (uint8_t)(session >> 24);
	return 0;
}
