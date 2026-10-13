# On-board English speech AI dashboard demo

## Goal and approved scope

Add a dashboard-only speech recognition demo to `apps/zephyr-hello-shell`
on the NUCLEO-H755ZI-Q M7 Zephyr target. Use a pretrained TensorFlow Lite
Micro English yes/no keyword model; no training, cloud service, or
host-side inference. The board recognizes `yes`, `no`, `unknown`, and
`silence` from the USB microphone attached to CN13.

Leave all LED behavior unchanged. Preserve the existing microphone
listening, mouse hot-plug, camera, shell, and dashboard functionality.
Hebrew, custom keywords, full transcription, headset speaker output, and
M4 inference are out of scope.

## Existing capture and constraints

The verified Logitech H570e captures 48 kHz stereo signed 16-bit PCM.
The existing 250 ms PCM ring is single-producer/single-consumer; browser
streaming consumes it. Recognition must not read that ring competitively.
USB transfers complete in interrupt context, so neither preprocessing nor
inference may execute in the capture callback or block it.

The small recognition model's file size is not the total memory cost.
Measure available flash and RAM against the current board build before
choosing tensor arenas, thread stacks, and audio buffer sizes. Integrate
only the runtime operators and signal-processing dependencies required
by the selected model and its matching frontend.

## Model and audio processing

Use the upstream TensorFlow Lite Micro `micro_speech` pretrained
English yes/no model and its matching preprocessing implementation:

https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech

Pin the dependency revision and model artifacts, retain upstream license
and provenance, and provide reproducible integration instructions.
Do not replace the trained frontend with arbitrary MFCC or spectrogram
parameters: the selected model's input quantization, feature scaling,
windowing, and label order must match its reference implementation.

For the verified microphone, select or downmix stereo channels and use
anti-alias filtering before reducing 48 kHz to 16 kHz mono. Confirm
channel choice against measured microphone samples rather than assuming
both channels contain equivalent speech. Feed signed 16-bit mono input
into the matching frontend, using its documented 30 ms windows and
20 ms stride. Accumulate approximately one second of features as required
by the model's actual tensor shape.

Initially support the verified 48 kHz stereo 16-bit capture format.
If another format is selected, the speech feature reports unsupported
format explicitly; existing microphone capture/playback remains independent.

## Capture feed and execution

Fan out captured PCM into a separate bounded recognition queue alongside
the existing playback ring. Capture performs bounded copies and counter
updates only. A dedicated lower-priority Zephyr thread drains the queue,
resamples, preprocesses, and runs inference. Static bounded storage keeps
allocation and backlog predictable.

Track capture session changes, queue overflow, USB errors, and missed
transfers. Unplug, format change, or audio discontinuity invalidates the
partial recognition window and clears frontend state. Resume only after
enough new contiguous audio is available. Do not fill gaps with fabricated
silence or retain stale predictions as current results.

Run decisions on a rolling window with a fixed inference cadence selected
after measuring the board. Require score threshold and temporal agreement
before reporting a detected yes/no event, and suppress repeated events
from the same utterance. Keep raw winning class/model score distinct from
accepted keyword events. Scores are model outputs, not calibrated
probabilities of correctness.

## Dashboard and API

Add `GET /api/v1/speech`, served by the existing dashboard HTTP worker.
It returns a thread-safe snapshot only; requests never perform inference.
The response includes:

- Availability/state: unavailable, initializing, warming up, running,
  unsupported format, or error, with a readable reason.
- Current winning label and normalized model score, null when no valid
  prediction exists.
- Last accepted keyword, its sequence number, and board uptime timestamp;
  clear it on capture session reset.
- Last inference duration, processed-window count, audio-gap count,
  recognition-queue drops, and capture session identifier.

Add a `Speech AI` card following the existing dashboard styling and polling
patterns. Show current class, model score, inference time, and microphone/
model state. Highlight each newly accepted yes/no event without controlling
LEDs. Use the event sequence to distinguish a new utterance from a repeated
poll of the same result.

Recognition starts automatically when the model is initialized and a
supported microphone is ready; browser listening is optional. Display
unavailable/error states explicitly and clear stale current predictions
when the microphone disconnects or API polling fails.

Native simulation retains the existing application behavior and reports
speech recognition unavailable without pretending to recognize synthetic
or host microphone audio. Any test-only injected PCM is labeled as a test
fixture, not live board recognition.

## Error handling

Model initialization, tensor allocation, operator mismatch, inference
failure, unsupported input, and capture interruptions must have explicit
status/reason reporting and repository-standard logging. Fail the speech
feature independently where possible; keep dashboard and microphone
playback operational. Dependency/build failures remain hard errors,
not silently disabled successful builds.

## Verification and acceptance

First compare model inference and preprocessing on known upstream yes/no
fixtures against reference outputs. Test resampling frequency rejection,
channel handling, chunk boundaries, frontend continuity, bounded overflow,
capture-session reset, decision suppression, and API/UI state transitions.
Preserve existing USB, web, and audio tests.

Build the M7 target with the Zephyr SDK ARM toolchain and the existing
native_sim profiles. Record flash/RAM use, tensor arenas, peak thread
stack use, inference duration, and queue counters. No claim of model fit
or real-time performance is valid before those measurements.

On physical hardware, speak at least ten separate `yes` and ten separate
`no` utterances in a quiet room. Target at least eight correct accepted
events per word, within two seconds after the word ends. Check a minute
of silence and unrelated speech for false keyword events, targeting at
most one false event during that interval. These are demo acceptance
checks, not a general accuracy benchmark.

Repeat recognition while browser microphone playback is active. Verify
intelligible playback, responsive dashboard/camera, no recognition queue
overflow attributable to inference throughput, and no new sustained USB
capture errors. Test unplug/replug and switching back to the USB mouse.
Document measured results, model limitations, endpoints, and operating
instructions in the application README section.

If hardware is unavailable or the thresholds are not met, report that
limitation and the observed results rather than claiming a verified demo.
