/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_AUDIO_CAPTURE_H_
#define USB_AUDIO_CAPTURE_H_

#include <stddef.h>
#include <stdint.h>

#include "usb_audio_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Current microphone state; safe from any thread. */
void usb_audio_snapshot_get(struct usb_audio_snapshot *out);

/*
 * Copy up to capacity bytes of whole interleaved PCM frames captured in
 * the given session. Returns 0 when the session is stale or idle.
 */
size_t usb_audio_read(uint32_t session, uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_CAPTURE_H_ */
