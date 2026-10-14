# Pretrained English speech models

Runtime and model source: Apache-2.0 licensed
[`zephyrproject-rtos/tflite-micro`](https://github.com/zephyrproject-rtos/tflite-micro),
commit `8d404de73acf7687831e16d88e86e4f73cfddf8e` (Zephyr 4.2.2 optional manifest pin).
Initialize with `git submodule update --init --recursive`.

Models are embedded from the pinned submodule by `cmake/speech_model.cmake`;
no training or download occurs during builds. Upstream license and copyright
notices remain in that submodule.

| File under `tensorflow/lite/micro/examples/micro_speech/models/` | Bytes | Git blob |
|---|---:|---|
| `micro_speech_quantized.tflite` | 18800 | `4d10b2ee2378c4f85ce5c58b2bd11ce18b3d09be` |
| `audio_preprocessor_int8.tflite` | 8772 | `790087b76697027f7026a75a92449e189d0beb99` |

Input: 16 kHz signed 16-bit mono; 480-sample frames advancing by 320 samples;
49 x 40 int8 features. Output order: silence, unknown, yes, no.
The exact upstream preprocessing model is used, not arbitrary MFCC features.
Scores are quantized model outputs, not measured accuracy.

Reference tests use upstream WAV fixtures and expected feature vectors.
Initial host arena measurements: frontend 10576 bytes; classifier 7504 bytes.
Both arenas reserve 16384 bytes; ARM/CMSIS-NN usage is checked separately.
