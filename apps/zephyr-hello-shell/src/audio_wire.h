/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AUDIO_WIRE_H_
#define AUDIO_WIRE_H_

#include <stdint.h>

int audio_wire_header(uint8_t out[16], uint32_t rate, uint16_t channels,
		      uint16_t bits, uint32_t session);

#endif /* AUDIO_WIRE_H_ */
