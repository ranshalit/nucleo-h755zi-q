# Speech AI Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recognize English yes/no on the STM32H755 M7 from the existing USB microphone and display results on the board dashboard, without changing LEDs.

**Architecture:** Give inference its own bounded capture feed, independent of the browser playback ring. A Zephyr worker resamples verified microphone PCM and runs the pinned upstream preprocessing and recognition models. HTTP and browser code consume snapshots; neither performs inference.

**Tech Stack:** Zephyr 4.2.2, Zephyr SDK ARM toolchain, TensorFlow Lite Micro C++17, existing CMSIS-NN module, C audio queue/decimator/decision helpers, existing embedded HTML/CSS/JavaScript.

## Global Constraints

- Approved spec: `docs/superpowers/specs/2026-10-13-speech-ai-demo-design.md`.
- Board: `nucleo_h755zi_q/stm32h755xx/m7`; inference remains on M7.
- No training, cloud service, host-side inference, Hebrew, or transcription.
- Leave all LED behavior unchanged.
- Preserve microphone playback, mouse hot-plug, camera, shell, and dashboard.
- Initially accept only verified 48 kHz stereo signed 16-bit PCM.
- Recognition uses 16 kHz mono, 30 ms frames, 20 ms stride, 49 x 40 int8 features.
- Labels, in tensor order: `silence`, `unknown`, `yes`, `no`.
- No inference, resampling, frontend execution, blocking, or allocation in USB interrupt context.
- Reset partial windows/frontend on capture session changes, USB gaps, and recognition queue overflow.
- Report unavailable, initializing, warming-up, running, unsupported-format, and error states explicitly.
- Native simulation reports unavailable; do not fabricate live recognition.
- Hardware acceptance: at least 8/10 separate utterances for each word accepted within two seconds after speech ends; at most one false event over one minute of silence/unrelated speech.
- Every implementation commit includes `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- Preserve unrelated untracked `docs/usb_suggest*.md`.

## Verified context and dependency choice

Zephyr's optional manifest pins `zephyrproject-rtos/tflite-micro` at
`8d404de73acf7687831e16d88e86e4f73cfddf8e`. Use that revision, not upstream
`main`, so runtime, static third-party dependencies, and models agree.
The optional module is not installed in this workspace. Its module metadata
uses Zephyr's external CMake/Kconfig integration, already available under
`vendor/zephyrproject/zephyr/modules/tflite-micro`.

The pinned model files are:

```text
tensorflow/lite/micro/examples/micro_speech/models/micro_speech_quantized.tflite
  18800 bytes, Git blob 4d10b2ee2378c4f85ce5c58b2bd11ce18b3d09be
tensorflow/lite/micro/examples/micro_speech/models/audio_preprocessor_int8.tflite
  8772 bytes, Git blob 790087b76697027f7026a75a92449e189d0beb99
```

Zephyr's module CMake builds many normal operators but only part of the signal
kernel set. The application must add missing signal kernels and their
dependencies explicitly; do not assume enabling TFLM alone builds the
micro_speech frontend.

Existing build map reports `_flash_used=0xc4ea4` out of 1 MiB and
`_image_ram_size=0x376b0` out of 512 KiB. Treat those as historical
measurements; build fresh baseline. Flash headroom is tighter than RAM.
Do not remove existing features or alter flash partitions to hide overflow.

## File ownership and interfaces

Create under `apps/zephyr-hello-shell/src/speech/`:

- `speech_audio.{h,c}`: platform-independent bounded PCM queue, session/gap tracking, streaming FIR decimator.
- `speech_decision.{h,c}`: model-score acceptance and repeated-event suppression.
- `speech_model.{h,cc}`: pinned TFLM models, operator registration, feature rolling window, inference and frontend reset.
- `speech_service.{h,c}`: Zephyr locking/worker, public capture ingress, API snapshot, UART diagnostics; simulator unavailable implementation selected at compile time.
- `speech_json.{h,c}`: bounded snapshot JSON serialization, separately host-testable.

Create `apps/zephyr-hello-shell/cmake/speech_model.cmake` for model embedding
and missing signal dependencies. Add pinned dependency submodule at
`apps/zephyr-hello-shell/third_party/tflite-micro` and provenance/license
notes at `apps/zephyr-hello-shell/third_party/speech-models/README.md`.

Create tests under `apps/zephyr-hello-shell/tests/speech/`,
`apps/zephyr-hello-shell/tests/web/test_speech_card.cjs`, and host/Zephyr test
runner `scripts/test_zephyr_speech.sh`. Runtime fixture test gets its own
Zephyr application under `apps/zephyr-hello-shell/tests/speech/model/`.

Public service boundary, shared by capture, HTTP, shell, and simulator:

```c
enum speech_state {
    SPEECH_UNAVAILABLE, SPEECH_INITIALIZING, SPEECH_WARMING_UP,
    SPEECH_RUNNING, SPEECH_UNSUPPORTED_FORMAT, SPEECH_ERROR
};
enum speech_label {
    SPEECH_SILENCE, SPEECH_UNKNOWN, SPEECH_YES, SPEECH_NO
};
struct speech_snapshot {
    enum speech_state state;
    const char *reason;
    bool prediction_valid;
    enum speech_label label;
    uint16_t score_permille;
    bool event_valid;
    enum speech_label event_label;
    uint32_t event_sequence;
    int64_t event_uptime_ms;
    uint32_t inference_us;
    uint32_t windows;
    uint32_t gaps;
    uint32_t drops;
    uint32_t session;
};
int speech_service_start(void);
void speech_capture_push(const struct usb_audio_snapshot *capture,
                         const uint8_t *pcm, size_t bytes);
void speech_capture_changed(const struct usb_audio_snapshot *capture);
void speech_snapshot_get(struct speech_snapshot *out);
int speech_snapshot_json(const struct speech_snapshot *snapshot,
                         char *out, size_t capacity);
```

Capture ingress is ISR-safe and copies only complete valid frames. Reason
strings have static lifetime. Worker alone owns model state. Snapshot reads
and capture writes use short spinlocks; model calls never hold those locks.

---

### Task 1: Pinned model runtime and reference inference

**Files:** Modify `.gitmodules`, application `CMakeLists.txt`, `Kconfig`, and
M7 board `.conf`; create dependency submodule, model CMake, provenance notes,
`src/speech/speech_model.{h,cc}`, and model test application.

**Produces:**

```c
int speech_model_init(void);
int speech_model_reset(void);
int speech_model_push_16k(const int16_t *samples, size_t count);
bool speech_model_window_ready(void);
int speech_model_invoke(uint16_t scores_permille[4], uint32_t *duration_us);
```

Use return `0` for success and negative errno for errors. Reset clears all
frontend state and buffered samples/features; it does not reuse a stale
window. Initialize interpreters and arenas once, not once per inference.

- [ ] Build fresh baseline:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only --build-dir build/zephyr-hello-shell-speech-baseline
```

Record memory summary; expect successful M7 build without speech changes.

- [ ] Add pinned dependency:

```bash
git submodule add https://github.com/zephyrproject-rtos/tflite-micro apps/zephyr-hello-shell/third_party/tflite-micro
git -C apps/zephyr-hello-shell/third_party/tflite-micro checkout --detach 8d404de73acf7687831e16d88e86e4f73cfddf8e
git -C apps/zephyr-hello-shell/third_party/tflite-micro hash-object tensorflow/lite/micro/examples/micro_speech/models/micro_speech_quantized.tflite
git -C apps/zephyr-hello-shell/third_party/tflite-micro hash-object tensorflow/lite/micro/examples/micro_speech/models/audio_preprocessor_int8.tflite
```

Expect the two blob IDs listed above. Preserve the module's licenses.

- [ ] Write failing Zephyr fixture test using the pinned upstream
`micro_speech_test.cc` reference audio and expected feature arrays. Test
30 ms yes/no features exactly, then full one-second yes/no/silence fixtures.
Core assertions:

```c
assert(speech_model_init() == 0);
assert(speech_model_reset() == 0);
assert(speech_model_push_16k(g_yes_1000ms_audio_data, 16000) == 0);
assert(speech_model_window_ready());
uint16_t scores[4];
uint32_t duration;
assert(speech_model_invoke(scores, &duration) == 0);
assert(scores[SPEECH_YES] > scores[SPEECH_NO]);
assert(scores[SPEECH_YES] > scores[SPEECH_UNKNOWN]);
assert(scores[SPEECH_YES] > scores[SPEECH_SILENCE]);
```

Run the test application with `west build -b native_sim/native/64`;
expect missing model functions before implementing them. Do not make this
fixture application the normal simulator's live recognition implementation.

- [ ] Add TFLM module to `ZEPHYR_EXTRA_MODULES` before `find_package(Zephyr)`.
Board config:

```text
CONFIG_APP_SPEECH_AI=y
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_TENSORFLOW_LITE_MICRO=y
CONFIG_TENSORFLOW_LITE_MICRO_CMSIS_NN_KERNELS=y
```

Declare `APP_SPEECH_AI` with dependency on the real USB host target. Enable
TFLM independently in the fixture test application. Follow hello_world's
`no_threadsafe_statics` C++ compile option. Embed both flatbuffers as aligned
read-only bytes using Zephyr's `generate_inc_file_for_target`; do not hand
transcribe binary model data.

- [ ] Implement model wrapper from the pinned reference test. Recognition
resolver registers reshape, fully-connected, depthwise convolution, softmax.
Preprocessing resolver registers the pinned model's 18 required operators.
Add missing signal kernel/source dependencies to application CMake without
duplicating sources already compiled by Zephyr. Validate schema, tensor
types, total input elements (1960), four output elements, and quantization.
Use separate initially bounded 64 KiB arenas; measure used sizes and tighten
after fixture success. Maintain a 480-sample input window advancing by 320
samples, and 49 consecutive 40-element feature rows. Dequantize all four
outputs with the actual output tensor scale and zero point.

Operator registration must propagate errors:

```cpp
if (resolver.AddDepthwiseConv2D() != kTfLiteOk) {
    return -EINVAL;
}
if (interpreter.AllocateTensors() != kTfLiteOk) {
    return -ENOMEM;
}
if (interpreter.Invoke() != kTfLiteOk) {
    return -EIO;
}
```

- [ ] Run fixture application on native_sim and M7 or existing Cortex-M7
QEMU target. Expect reference feature equality and winning yes/no/silence
classes. Also build normal M7 application to confirm actual flash fit.
If arena/schema/operator errors occur, report the exact error and fix the
dependency/preprocessing mismatch rather than substituting fake scores.

- [ ] Commit dependency, wrapper, fixtures, and build integration:

```bash
git commit -m "feat(speech): integrate pretrained keyword model" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 2: Independent bounded audio feed and decimation

**Files:** Create `speech_audio.{h,c}`, `tests/speech/test_speech_audio.c`,
and `scripts/test_zephyr_speech.sh`.

**Produces:** `speech_audio_reset`, `speech_audio_push`,
`speech_audio_read`, and `speech_decimator_process`, with explicit context
structs passed by pointer. Queue capacity is 48,000 raw bytes (250 ms at the
verified format); overflow discards stale queued audio, increments drops and
discontinuity generation, and starts a new contiguous segment. Queue is not
internally locked; service supplies locking.

- [ ] Add failing tests for independent queue reads, complete-frame alignment,
session change, malformed packet length, overflow, and no stale audio after
reset. Example session assertion:

```c
struct speech_audio_queue queue;
uint8_t out[192];
speech_audio_reset(&queue, 1);
assert(speech_audio_push(&queue, 1, packet, sizeof(packet)) == sizeof(packet));
speech_audio_reset(&queue, 2);
assert(speech_audio_read(&queue, 1, out, sizeof(out)) == 0);
assert(speech_audio_read(&queue, 2, out, sizeof(out)) == 0);
```

- [ ] Compile with `cc -std=c11 -Wall -Wextra -Werror` through the new
runner, using only host-independent speech sources. Expect failure before
implementation; then implement queue and rerun.

- [ ] Implement streaming 3:1 FIR decimator with persistent history and phase.
Use a normalized windowed-sinc low-pass, 63 taps and 6 kHz cutoff at 48 kHz,
with reproducible checked-in Q15 coefficients. Accumulate in int64,
round/saturate output to int16. No filter work in capture ingress.
Default to left channel; inspect real channels before finalizing.
Channel selection must be deterministic and documented.

- [ ] Add numerical tests: one 48 kHz second produces exactly 16,000
samples; processing arbitrary complete-frame chunks equals one contiguous
call; a 1 kHz tone is within 1 dB of passband level after settling; a 12 kHz
tone is suppressed by at least 30 dB relative to input; opposite stereo
channels do not accidentally cancel when left-channel mode is selected.
Generate test tones with host `sin()` and link `-lm`; compute RMS only after
filter settling. Run runner and require all assertions to pass.

- [ ] Commit:

```bash
git commit -m "feat(speech): add bounded audio feed and resampling" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 3: Decision acceptance and repeated-event suppression

**Files:** Create `speech_decision.{h,c}` and
`tests/speech/test_speech_decision.c`; extend test runner.

**Consumes:** Four model scores in label order, each 0..1000, monotonic board
uptime, and capture session generation.

**Produces:**

```c
void speech_decision_reset(struct speech_decision *decision);
bool speech_decision_update(struct speech_decision *decision,
                            const uint16_t scores[4], int64_t uptime_ms,
                            enum speech_label *event);
```

- [ ] Write failing tests: yes/no need three consecutive winning scores
at least 800, evaluated every 100 ms; ties or scores below threshold do not
trigger; unknown/silence never become keyword events; sustained yes triggers
once; a new utterance requires 300 ms of non-keyword state and 1000 ms since
the prior event; reset removes candidate and suppression state.
Example:

```c
uint16_t yes[4] = {0, 50, 900, 50};
enum speech_label event;
speech_decision_reset(&decision);
assert(!speech_decision_update(&decision, yes, 100, &event));
assert(!speech_decision_update(&decision, yes, 200, &event));
assert(speech_decision_update(&decision, yes, 300, &event));
assert(event == SPEECH_YES);
assert(!speech_decision_update(&decision, yes, 400, &event));
```

- [ ] Implement bounded decision context, separate from raw winning class.
Use int64 monotonic timestamp arithmetic; no wall clock or heap.
- [ ] Run host runner; require suppression, reset, and unrelated-speech
tests to pass. Thresholds are initial values; tune only against recorded
hardware evidence and rerun all decision tests.
- [ ] Commit:

```bash
git commit -m "feat(speech): stabilize keyword events" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 4: Capture fan-out and Zephyr inference service

**Files:** Create `speech_service.{h,c}`; modify `usb_audio_capture.c`,
`main.c`, and application CMake. Extend host service tests with existing
Zephyr shims as needed.

**Consumes:** Model wrapper from Task 1, queue/decimator from Task 2,
decision helper from Task 3.
**Produces:** Public service interface listed above, plus shell command
`speech status`.

- [ ] Write tests for start failure, ready/warming/running transitions,
unsupported format, reset on session/error/missed-counter changes, overflow,
unplug, and failure isolation. Capture fan-out must leave the original
playback ring's bytes and counters unchanged.
- [ ] Add capture hook only after successful whole-frame validation:

```c
if (after.errors == before.errors) {
    speech_capture_push(&after, audio.packet, length);
}
```

Call outside the original audio spinlock. Zero-length transfers add no PCM.
Feed lifecycle changes from ready, fail, detach, and attach paths via
`speech_capture_changed`, also outside the original lock.
Use the same generation metadata on ISR and thread ingress to reject stale
callbacks after disconnect. Never call `usb_audio_read` from inference.

- [ ] Implement service worker with a static initial 8 KiB stack and
preemptible priority 8; verify USB pump/network priorities before finalizing.
Drain raw data in bounded chunks, decimate and generate features, invoke
every five feature advances (100 ms) once 49 rows exist. Block on notification
when idle; never spin when no microphone is present.
Only inference thread accesses interpreters.

- [ ] Maintain thread-safe snapshots. Every reset invalidates prediction
and last event, clears queue/filter/frontend/decision state, then warms up on
fresh audio. Keep event sequence monotonic across session resets so browser
cannot mistake an old event for a new one. Count gaps and drops separately.
Log transitions/failures, not every PCM packet.
Simulator functions report `SPEECH_UNAVAILABLE` and a static readable reason.

- [ ] Start service from `main` without preventing the existing HTTP/audio/
camera startup when speech initialization fails. Shell reports state, model
score, last event, arena use, timing, queue drops, and gaps. Do not modify LED
threads or commands.
- [ ] Run host tests, model fixtures, M7 build and both native_sim builds.
Expect no simulation model dependency when the live feature is unavailable.
- [ ] Commit:

```bash
git commit -m "feat(speech): run inference from USB capture" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 5: Snapshot API and dashboard recognition card

**Files:** Create `speech_json.{h,c}`, serializer tests and
`tests/web/test_speech_card.cjs`; modify `http_server.c`, `web_assets.c`,
and test runner.

**Consumes:** `speech_snapshot_get`.
**Produces:** `GET /api/v1/speech` and Speech AI dashboard card.

- [ ] Write failing JSON tests for unavailable, warmup, yes/no, inference
error, truncation, and nullable label/score/event. Exact response shape:

```json
{
  "state": "running",
  "reason": "recognizing English yes/no",
  "label": "yes",
  "score": 0.91,
  "last_event": {"label": "yes", "sequence": 7, "uptime_ms": 12500},
  "inference_us": 12000,
  "windows": 32,
  "gaps": 0,
  "drops": 0,
  "session": 3
}
```

Emit `label`, `score`, and `last_event` as JSON null when invalid.
Serialize scores using integer whole/fraction components, not float printf.
Escape readable reason text safely or restrict it to static serializer-owned
messages. Return `-ENOSPC` for insufficient output capacity; HTTP uses existing
`send_internal_error`, not truncated successful JSON.

- [ ] Register resource with existing GET/JSON callback pattern, no-store,
bounded static response buffer, and snapshot-only handling:

```c
HTTP_RESOURCE_DEFINE(speech_resource, board_http_service, "/api/v1/speech",
                     &speech_resource_detail);
```

- [ ] Add DOM IDs `speech-state`, `speech-label`, `speech-score`,
`speech-event`, `speech-timing`, and `speech-health`, following existing card
styling. Display "Model score", not "accuracy". Poll every 500 ms using a
separate loop that does not overlap requests. Clear stale predictions/events
on unavailable/warmup/error and polling failure. Highlight accepted events
only when session/sequence indicates a new event.
No LED POST requests from speech UI.

- [ ] Write browser test following `test_mic_player.cjs`'s C-string extraction
and local fixture-server pattern. Feed unavailable, warmup, yes, repeated
same event, no, unplug, HTTP failure, recovery. Assert labels, null handling,
one highlight per event, clearing stale state, and zero speech-triggered LED
requests. Add default unavailable `/api/v1/speech` responses to existing web
test fixture servers so old tests remain noise-free.

- [ ] Run:

```bash
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_mic_player.cjs
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_camera_toggle.cjs
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_value_glow.cjs
```

Expect all tests to pass. If `puppeteer-core` is absent, resolve the missing
dependency using the existing browser tooling, not by removing assertions.

- [ ] Commit:

```bash
git commit -m "feat(web): display on-board speech recognition" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 6: Hardware acceptance and documentation

**Files:** Modify root `README.md`, provenance notes, and directly related
test/build instructions. Tune only speech settings justified by measurements.

- [ ] Run complete speech runner and existing USB/camera host tests with
their checked-in compiler procedures. Build M7 and native_sim:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only --build-dir build/zephyr-hello-shell-speech
scripts/run_zephyr_hello_shell_native.sh --profile localhost --build-only
scripts/run_zephyr_hello_shell_native.sh --profile tap --build-only
```

- [ ] Before device interaction, invoke the applicable board-access skill.
Use the verified STM32 flash script, not a Jetson flash flow. Check routing
to `192.168.55.2` uses the physical NIC, not stale simulator `zeth`.
Flash exact new build:

```bash
scripts/flash_zephyr_hello_shell.sh --flash-only --build-dir build/zephyr-hello-shell-speech
curl --fail --silent http://192.168.55.2/api/v1/speech
```

Expect warming-up then running with the verified headset, or explicit
unavailable reason without it. UART `speech status` must match API state.

- [ ] Measure live microphone left/right channel signal and verify the chosen
channel contains intelligible speech. Speak ten separate yes and ten no
utterances; record accepted sequences, correct/wrong events and latency.
User participation may be required for spoken utterances; prerecorded
fixtures alone do not satisfy physical microphone acceptance.
- [ ] Measure one minute of silence/unrelated speech, then repeat with
browser playback and camera active. Inspect queue overflow/gap/USB counters,
API responsiveness and thread stack use. Require thresholds in approved
spec; record observed results rather than inferring success from build.
- [ ] Test headset unplug/replug, unsupported format status, and replacement
with existing mouse. Check native_sim card reads unavailable.
- [ ] Tighten arenas to measured required size plus documented safety margin;
verify M7 flash/RAM and stack use. Any precision/performance tuning must keep
fixture equivalence and pass all decision and resampling tests.
- [ ] Document connection, English-only vocabulary, offline inference,
automatic recognition independent of listening, score meaning, model
provenance, reproducible dependency initialization, `/api/v1/speech`,
`speech status`, actual memory/timing, and measured limitations.
If real hardware or spoken testing is unavailable, mark hardware acceptance
incomplete and state exactly which checks remain.
- [ ] Commit final measured integration/documentation:

```bash
git commit -m "docs(speech): document keyword demo and limits" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Plan self-review

Spec coverage: Task 1 covers provenance/runtime/frontend equivalence;
Task 2 covers independent capture feed/resampling; Task 3 covers accepted
events; Task 4 covers execution, continuity, errors, shell and simulation;
Task 5 covers bounded API and dashboard; Task 6 covers coexistence,
hardware measurements and documentation. No task changes LEDs or requires
training. Complete the reference/runtime gate before claiming live speech
recognition, and complete measured hardware checks before claiming verified
board accuracy.
