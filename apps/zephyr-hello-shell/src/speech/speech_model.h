/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_SPEECH_MODEL_H
#define APP_SPEECH_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Classifier output order: silence, unknown, yes, no, up, down, left, right, stop. */
#define SPEECH_MODEL_LABELS 9
#ifdef __cplusplus
extern "C" {
#endif
int speech_model_init(void);
int speech_model_reset(void);
int speech_model_push_16k(const int16_t *samples, size_t count);
bool speech_model_window_ready(void);
int speech_model_invoke(uint16_t scores[SPEECH_MODEL_LABELS], uint32_t *duration_us);
size_t speech_model_arena_used(bool frontend);
/* Re-run the classifier on its current input; call only from the speech worker. */
int speech_model_bench(unsigned int runs, uint32_t *min_us, uint32_t *max_us);
int speech_model_latest_feature(int8_t out[40]);
#ifdef __cplusplus
}
#endif
#endif
