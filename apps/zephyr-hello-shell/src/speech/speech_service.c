/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_service.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
static struct k_spinlock lock;
static struct speech_snapshot status = {
	.state = SPEECH_UNAVAILABLE, .reason = "speech recognition unavailable in simulation"
};

#ifdef CONFIG_APP_SPEECH_AI
#include "speech_audio.h"
#include "speech_decision.h"
#include "speech_model.h"
static struct speech_audio_queue queue;
static struct speech_decimator decimator;
static struct speech_decision decision;
static struct usb_audio_snapshot input;
static bool started;
static bool failed;
static bool model_ready;
K_SEM_DEFINE(audio_ready, 0, 1);
K_THREAD_STACK_DEFINE(worker_stack, 8192);
static struct k_thread worker;

static void invalidate_locked(void)
{
	status.prediction_valid = false;
	status.event_valid = false;
	status.inference_us = 0;
	status.processing_us = 0;
}

static bool supported(const struct usb_audio_snapshot *capture)
{
	return capture->sample_rate_hz == 48000 && capture->channels == 2 &&
	       capture->bits_per_sample == 16;
}

static void capture_changed_locked(const struct usb_audio_snapshot *capture)
{
	bool reset = input.session != capture->session || input.ready != capture->ready ||
		     input.connected != capture->connected || input.errors != capture->errors ||
		     input.missed != capture->missed || input.sample_rate_hz != capture->sample_rate_hz ||
		     input.channels != capture->channels || input.bits_per_sample != capture->bits_per_sample;
	bool gap = input.session == capture->session && input.ready && capture->ready &&
		   (input.errors != capture->errors || input.missed != capture->missed);
	input = *capture;
	if (!reset) { return; }
	speech_audio_reset(&queue, capture->session);
	status.session = capture->session;
	if (gap) { ++status.gaps; }
	invalidate_locked();
	if (failed) { return; }
	if (!capture->connected || !capture->ready) {
		status.state = SPEECH_UNAVAILABLE;
		status.reason = "no ready USB microphone";
	} else if (!supported(capture)) {
		status.state = SPEECH_UNSUPPORTED_FORMAT;
		status.reason = "speech requires 48000 Hz stereo 16-bit PCM";
	} else {
		status.state = model_ready ? SPEECH_WARMING_UP : SPEECH_INITIALIZING;
		status.reason = "collecting contiguous microphone audio";
	}
}

void speech_capture_changed(const struct usb_audio_snapshot *capture)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	capture_changed_locked(capture);
	k_spin_unlock(&lock, key);
	k_sem_give(&audio_ready);
}

void speech_capture_push(const struct usb_audio_snapshot *capture,
			 const uint8_t *pcm, size_t bytes)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	capture_changed_locked(capture);
	if (!failed && capture->connected && capture->ready && supported(capture)) {
		uint32_t generation = queue.generation;
		if (bytes % 4 || bytes > SPEECH_QUEUE_BYTES || (!pcm && bytes)) {
			++status.gaps;
			speech_audio_reset(&queue, capture->session);
			invalidate_locked();
			status.state = SPEECH_WARMING_UP;
			status.reason = "invalid microphone packet";
		} else {
			(void)speech_audio_push(&queue, capture->session, pcm, bytes);
			status.drops = queue.drops;
			if (generation != queue.generation) {
				++status.gaps;
				invalidate_locked();
				status.state = SPEECH_WARMING_UP;
				status.reason = "recognition queue overflow";
			}
		}
	}
	bool wake = queue.count >= 960;
	k_spin_unlock(&lock, key);
	if (wake) { k_sem_give(&audio_ready); }
}

static void model_error(int ret)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	failed = true;
	status.state = SPEECH_ERROR;
	status.reason = "model initialization or inference failed; see UART log";
	invalidate_locked();
	k_spin_unlock(&lock, key);
	printk("speech: model failed: %d\n", ret);
}

static void run_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int ret = speech_model_init();
	if (ret) { model_error(ret); return; }
	printk("speech: English yes/no model ready; arenas frontend=%u classifier=%u\n",
		(unsigned)speech_model_arena_used(true), (unsigned)speech_model_arena_used(false));
	k_spinlock_key_t key = k_spin_lock(&lock);
	model_ready = true;
	if (!input.connected || !input.ready) {
		status.state = SPEECH_UNAVAILABLE;
		status.reason = "no ready USB microphone";
	} else if (!supported(&input)) {
		status.state = SPEECH_UNSUPPORTED_FORMAT;
		status.reason = "speech requires 48000 Hz stereo 16-bit PCM";
	} else {
		status.state = SPEECH_WARMING_UP;
		status.reason = "collecting contiguous microphone audio";
	}
	k_spin_unlock(&lock, key);
	uint32_t active_generation = UINT32_MAX;
	size_t since_inference = 0;
	uint8_t pcm[960];
	int16_t mono[80];
	while (true) {
		(void)k_sem_take(&audio_ready, K_MSEC(20));
		while (true) {
			key = k_spin_lock(&lock);
			uint32_t generation = queue.generation;
			bool ready = input.connected && input.ready && supported(&input);
			size_t bytes = ready ? speech_audio_read(&queue, input.session, pcm, sizeof(pcm)) : 0;
			k_spin_unlock(&lock, key);
			if (generation != active_generation) {
				speech_decimator_reset(&decimator);
				speech_decision_reset(&decision);
				ret = speech_model_reset();
				if (ret) { model_error(ret); return; }
				active_generation = generation;
				since_inference = 0;
			}
			if (!bytes) { break; }
			uint32_t start = k_cycle_get_32();
			size_t samples = speech_decimator_process(&decimator, pcm, bytes, mono);
			ret = speech_model_push_16k(mono, samples);
			if (ret) { model_error(ret); return; }
			since_inference += samples;
			bool infer = speech_model_window_ready() && since_inference >= 1600;
			uint16_t scores[4];
			uint32_t inference_us = 0;
			enum speech_label event = SPEECH_UNKNOWN;
			bool accepted = false;
			unsigned int winner = 0;
			if (infer) {
				since_inference %= 1600;
				ret = speech_model_invoke(scores, &inference_us);
				if (ret) { model_error(ret); return; }
				for (unsigned int i = 1; i < 4; ++i) {
					if (scores[i] > scores[winner]) { winner = i; }
				}
				accepted = speech_decision_update(&decision, scores, k_uptime_get(), &event);
			}
			uint32_t processing_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
			key = k_spin_lock(&lock);
			if (queue.generation == active_generation) {
				status.processing_us = processing_us;
				if (processing_us > status.max_processing_us) { status.max_processing_us = processing_us; }
				if (infer) {
					status.state = SPEECH_RUNNING;
					status.reason = "recognizing English yes/no";
					status.prediction_valid = true;
					status.label = (enum speech_label)winner;
					status.score_permille = scores[winner];
					status.inference_us = inference_us;
					++status.windows;
					if (accepted) {
						status.event_valid = true;
						status.event_label = event;
						++status.event_sequence;
						status.event_uptime_ms = k_uptime_get();
					}
				}
			}
			k_spin_unlock(&lock, key);
			/* Give lower-priority host recovery/polling time between chunks. */
			k_msleep(1);
		}
	}
}

int speech_service_start(void)
{
	if (started) { return 0; }
	started = true;
	k_spinlock_key_t key = k_spin_lock(&lock);
	status.state = SPEECH_INITIALIZING;
	status.reason = "initializing English yes/no model";
	k_spin_unlock(&lock, key);
	k_tid_t tid = k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
				     run_worker, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(tid, "speech_ai");
	return 0;
}
#else
int speech_service_start(void) { return -ENOTSUP; }
void speech_capture_changed(const struct usb_audio_snapshot *capture) { ARG_UNUSED(capture); }
void speech_capture_push(const struct usb_audio_snapshot *capture, const uint8_t *pcm, size_t bytes)
{
	ARG_UNUSED(capture); ARG_UNUSED(pcm); ARG_UNUSED(bytes);
}
#endif

void speech_snapshot_get(struct speech_snapshot *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	*out = status;
	k_spin_unlock(&lock, key);
}

static int cmd_speech_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	struct speech_snapshot snapshot;
	speech_snapshot_get(&snapshot);
	shell_print(shell, "speech state=%d reason=%s session=%u windows=%u gaps=%u drops=%u",
		    snapshot.state, snapshot.reason, snapshot.session, snapshot.windows,
		    snapshot.gaps, snapshot.drops);
	shell_print(shell, "prediction valid=%u label=%d score=%u/1000 event=%u sequence=%u",
		    snapshot.prediction_valid, snapshot.label, snapshot.score_permille,
		    snapshot.event_valid, snapshot.event_sequence);
	shell_print(shell, "inference=%u us processing=%u us max_processing=%u us",
		    snapshot.inference_us, snapshot.processing_us, snapshot.max_processing_us);
	return 0;
}
SHELL_STATIC_SUBCMD_SET_CREATE(speech_commands,
	SHELL_CMD(status, NULL, "Speech recognition status and timing", cmd_speech_status),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(speech, &speech_commands, "On-board English yes/no recognition", NULL);
