# Speech keyword model: noise rejection investigation

## Status (2026-10-14)

The experimental direction-keyword model is not ready for integration.
Retraining with room noise reduced false events on two captured reproductions,
but did not reliably reject whispered "shhhh" or microphone handling.
Keep direction support with v1 as the default model. Preserve v2 locally for
research rather than make it the default. Neither model reliably rejects these
noises. The user reversed the discard request and approved validating and merging
v1 together with current master and the runtime threshold controls.

, master still used the original yes/no model and the
board ran v2. The planned v1 integration replaces both after validation; reflashing
is required to change the model on the board.

## Models and firmware

TensorFlow Lite Micro (TFLM) is the inference runtime, not the model
architecture. The experiment uses TensorFlow's `tiny_conv` architecture,
trained on Speech Commands v0.02 and exported as an int8 TFLite model.

Both experimental models have eight output classes, in this order:
`silence, unknown, yes, no, up, down, left, right`. There are six keywords,
not eight. Each classifier is 35,000 bytes. Input quantization is
scale `0.10171568393707275`, zero point `-128`, matching the board frontend.
Operators are `CONV_2D`, `FULLY_CONNECTED`, `RESHAPE`, and `SOFTMAX`.

| Model | Training | Reported test accuracy |
|-------|----------|------------------------|
| Original | Upstream TFLM `micro_speech` yes/no model | Not remeasured in this investigation |
| Direction v1 | Six keywords, stock background noise | Float 83.0%; int8 82.55% (N=3055) |
| Room-noise v2 | Same architecture plus recorded room noise | Float 80.5% (N=3326); int8 81.73% (N=3055) |

The float v2 result uses a different silence/unknown class mix, so it is not
directly comparable to v1. Overall classification accuracy is also not a
measurement of continuous-stream false-trigger rate.

The experimental firmware placed the classifier and frontend in flash bank 2
at `0x08100000`, while inference still ran on Cortex-M7. M4 boot-at-init was
disabled; this was model storage in the unused M4 flash bank, not M4 inference.
The bank-2 flash operation overwrote the old M4 demo.

Last build: bank 1 1,005,836 bytes (95.92%), RAM 400,944 bytes (76.47%),
model bank-2 region 43,784 bytes. Live inference took about 22 ms.

## Investigation sequence

1. Trained v1 using TensorFlow r2.15 `speech_commands/train.py`:
   `--wanted_words=yes,no,up,down,left,right --preprocess=micro
   --window_stride=20 --model_architecture=tiny_conv
   --how_many_training_steps=12000,3000 --learning_rate=0.001,0.0001`.
   Silence and unknown percentages were each 12%.
2. Extended firmware labels, decision logic, JSON, dashboard, and fixtures.
   Unit/browser tests and v1 model fixtures passed after changing the
   near-zero silence expectation as explained below. Built and flashed v1.
3. User observed false direction commands in a noisy room. Captured one minute
   and then five minutes through the board's `MIC1` HTTP stream on port 8082
   (48 kHz stereo signed 16-bit PCM).
4. Converted recordings using the firmware's left-channel 63-tap FIR and
   3:1 decimation to 16 kHz mono. Four one-minute segments of the second
   recording, with 3x gain, were added to `_background_noise_`. Its second
   minute and the first recording were withheld from training.
5. Trained v2 from scratch, not by fine-tuning v1. Increased silence samples
   to 15%, unknown samples to 20%, and word background volume from 0-0.1 to
   0-0.3. Stock training mixes noise into 80% of word samples; silence samples
   use noise alone at random volume 0-1.
6. Training stopped when the terminal exited, then resumed saved checkpoint
   4000 and completed step 15000. Paused before export as requested.
7. After approval, exported v2, built it, and flashed for user testing first.
   Decision settings remained score >=800/1000, three consecutive eligible
   updates at 100 ms cadence, 300 ms quiet rearm, and 1 s event cooldown.
8. User still observed commands from "shhhh" and mic touching/releasing.
   Captured separate approximately 30-second reproductions and replayed
   identical audio through both models.

The threshold-control implementation was found in the separate
`feature/speech-threshold-setting` worktree. Its core, API and dashboard commits
are being integrated into the direction branch, retaining the eight-label
changes. The default stays 80%; runtime changes use the existing control.

## Results and limits

| Reproduction | v1 offline false events | v2 offline false events |
|--------------|-------------------------|-------------------------|
| Whispered "shhhh", approximately 30 s | 5 | 3 |
| Mic touching/releasing, approximately 30 s | 5 | 2 |

Live v2 polling also observed a new accepted `right` event in the "shhhh"
capture and a new accepted `no` event in the mic-handling capture. These are
accepted command events, not merely the dashboard's changing current class.
Labels differed between live and offline results. Offline replay uses the
TensorFlow Python microfrontend and a fresh stream state; it has not been
proven bit-for-bit equivalent to the live TFLM frontend and timing.

The API already reported 100 gaps and 1,200,000 dropped input frames before
these captures. Neither counter increased during either reproduction.
The earlier loss remains a separate concern, but new loss does not explain
these particular captured false events.

Before v2 training, an offline v1 baseline on approximately two withheld
minutes produced three false events (1.5/min). A synthetic streaming test of
360 held-out keyword clips mixed with room noise produced 60.3% keyword hits
at score 0.8 and three updates. Raising score to 0.95 and requiring five updates
eliminated events on that short noise sample but reduced keyword hits to 20.3%.
These are preliminary offline figures, not live-board recognition accuracy.
They show why simply raising the threshold is not an established fix.

The broader held-out noise/keyword comparison for v2 was not completed before
the user requested pausing the noise-improvement experiment.

Hypothesis: background-noise augmentation did not adequately cover close-up
hiss and microphone-contact sounds as negative examples. This is not yet a
confirmed root cause; retraining with explicit negatives has not been tested.
Adjacent minutes from one recording are disjoint audio, but not independent
recording sessions. Future final evaluation needs separately recorded sessions.

## Integration scheduling issue

The USB event worker had already passed microphone plus original yes/no speech
and network trials on master; see `docs/experiments/usb-event-worker/README.md`.
Those results do not imply failure in that earlier validation.

Integrating the larger v1 direction model with that worker reproduced fatal
FIFO overflow twice. Speech ran at priority 8, above USB priority 10, preventing
the notification consumer from preempting inference. Moving speech to priority
11 only when the blocking event worker is enabled retained priority 8 for the
busy-polling comparator and avoided the reproduced overflow.

With that change, a 60-second playback/inference/HTTP trial transferred
11,534,336 bytes, recorded FIFO high water 17/128 and overflow=0, and had no new
recognition gaps/drops. USB errors and missed frames were zero. A separate
60-second inference/API trial also had no new recognition gaps/drops.
This is bounded workload evidence, not a guarantee against arbitrary starvation.
Mic playback-ring evictions when no listener consumes it are distinct from
recognition queue drops or USB errors.

The integrated build uses 1,012,548 bytes of bank 1 (96.56%), 402,864 bytes of
RAM (76.84%), and 43,784 bytes of bank 2. Typical observed inference was 27 ms.
UART/LED checks, both ping directions, and the existing HTTP API checks passed;
the general validation script still falsely fails its root-page check because
it expects the old "STM32H7 board I/O" marker. A direct root-page check verified
the current dashboard, six-word list and threshold input. This pre-existing
validator mismatch was not changed as part of model integration.

## Near-zero silence fixture

The upstream `silence_1000ms.wav` fixture peaks at only 7/32767: nearly digital
zero, unlike a typical live mic. V1 predicted diffuse scores, with `no` highest
at 211/1000, below the event threshold. The test was changed from requiring
silence as the winning class to requiring every keyword score below 800/1000.
The stock noise fixture still classified as silence.

Silence was not omitted from training. However, passing this no-trigger fixture
does not establish rejection of real hiss, contact noise, or other unseen noise.

## Preserved artifacts

The original model is versioned inside the pinned TFLM submodule:
`apps/zephyr-hello-shell/third_party/tflite-micro/tensorflow/lite/micro/examples/micro_speech/models/micro_speech_quantized.tflite`.
The submodule revision used was `8d404de73acf7687831e16d88e86e4f73cfddf8e`.

V1 is now committed as `apps/zephyr-hello-shell/models/speech_keywords.tflite`.
V2 and training checkpoints remain local backups, **not tracked in git**:

- `/home/ranshal/kws-train/speech_keywords_v1.tflite`
  SHA-256 `effa7d8d35e3c2d06f2ac03d6d7bb63e2bb3508312b7278fefab4b6e991b0114`.
- `/home/ranshal/kws-train/speech_keywords_room_v2.tflite`
  SHA-256 `e1fe2fedf4528e271c76686cb49874a42e890493902bfa8a2a5959a3f302392f`.
- Checkpoints: `/home/ranshal/kws-train/train/tiny_conv.ckpt-15000`
  and `/home/ranshal/kws-train/train_room/tiny_conv.ckpt-15000`.
- Logs: `/home/ranshal/kws-train/train_room.log`, `export_room.log`,
  and `eval_v1.txt`.
- Saved experiment scripts:
  `/home/ranshal/kws-train/room_experiment/{train_speech_keywords.sh,export_speech_keywords.py,speech_capture_to_wav.py}`.
- Room recordings: `/home/ranshal/kws-train/{room_noise.raw,room_noise2.raw}`.
  Train WAVs: `room_train/`; withheld second-minute WAV: `room_all/room_01.wav`.
- Reproduction captures and event logs:
  `/home/ranshal/.copilot/session-state/a5156cb1-9a2d-46d6-9222-d37f0da8dba0/files/`,
  files `shhhh.raw`, `shhhh-events.json`, `mic-handling.raw`,
  and `mic-handling-events.json`. Offline scripts `replay.py` and
  `eval_decision.py` are in that folder too.

Recordings may contain voices; keep local unless sharing is explicitly approved.
These machine-local paths are not portable project assets. The failed
room-noise training scripts are archived locally; the checked-in training script
uses the v1 stock-noise recipe and rejects experimental added-noise datasets.

## Next investigation

- Reproduce hiss, handling, coughs, and distant speech in separately labeled
  sessions. Include clean and noisy real keyword utterances as positive controls.
- Verify replay frontend, window alignment, quantization, and event timing
  against board outputs before relying on exact offline event counts.
- Establish false events per hour and per-keyword recall/latency targets.
  Preserve held-out recording sessions; do not train on the final test clips.
- Test explicit non-command examples in the silence/unknown training pipeline.
  Change one factor at a time and compare both model and decision parameters.
- Investigate historical gaps/drops under USB, inference, and network load
  on the current base, which now includes the USB event worker.
- Direction support is a demo, not a reliable command interface. Improve it only
  after negative rejection and positive recall meet agreed targets. Preserve
  runtime threshold controls and documented bank-2 ownership/M4 boot requirements.
