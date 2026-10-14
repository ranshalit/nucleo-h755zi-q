# English speech models

Runtime and model source: Apache-2.0 licensed
[`zephyrproject-rtos/tflite-micro`](https://github.com/zephyrproject-rtos/tflite-micro),
commit `8d404de73acf7687831e16d88e86e4f73cfddf8e` (Zephyr 4.2.2 optional manifest pin).
Initialize with `git submodule update --init --recursive`.

The frontend is embedded from the pinned submodule by `cmake/speech_model.cmake`;
the classifier is embedded from `models/speech_keywords.tflite`. No training or
download occurs during builds. Upstream license and copyright notices remain
in the submodule. The original yes/no classifier remains available there for
reference but is no longer the application default.

| File under `tensorflow/lite/micro/examples/micro_speech/models/` | Bytes | Git blob |
|---|---:|---|
| `micro_speech_quantized.tflite` | 18800 | `4d10b2ee2378c4f85ce5c58b2bd11ce18b3d09be` |
| `audio_preprocessor_int8.tflite` | 8772 | `790087b76697027f7026a75a92449e189d0beb99` |

Input: 16 kHz signed 16-bit mono; 480-sample frames advancing by 320 samples;
49 x 40 int8 features. Output order: silence, unknown, yes, no, up, down, left, right.
The exact upstream preprocessing model is used, not arbitrary MFCC features.
Scores are quantized model outputs, not measured accuracy.

Reference tests use upstream WAV fixtures and expected feature vectors.
Direction fixtures come from the Speech Commands testing split; attribution
and source filenames are in `tests/speech/model/fixtures/README.md`.
Host arena measurements: frontend 10576 bytes; classifier 9696 bytes.
Both arenas reserve 16384 bytes; ARM/CMSIS-NN usage is checked separately.

## Default classifier: direction v1

- Architecture: TensorFlow r2.15 `speech_commands` `tiny_conv`, trained on
  Google Speech Commands v0.02 (CC BY 4.0 dataset; see
  <https://www.tensorflow.org/datasets/catalog/speech_commands>).
- Artifact: `models/speech_keywords.tflite`, 35000 bytes.
- SHA-256: `effa7d8d35e3c2d06f2ac03d6d7bb63e2bb3508312b7278fefab4b6e991b0114`.
- Recipe: `scripts/train_speech_keywords.sh`; 12000 steps at 0.001 and 3000
  at 0.0001, 12% silence, 12% unknown, stock background-noise augmentation.
- Int8 test accuracy: 82.55% (N=3055); silence 96.6%, unknown 53.0%, yes 90.7%,
  no 81.2%, up 87.5%, down 79.6%, left 80.8%, right 86.4%.
- Input scale `0.10171568393707275`, zero point `-128`; export checks these
  against the fixed upstream frontend.
- Operators: `CONV_2D`, `FULLY_CONNECTED`, `RESHAPE`, `SOFTMAX`. Int8-only
  classifier registrations avoid unused kernel variants in bank-1 flash.
- On the board, classifier and frontend are read-only data in bank 2; M4
  must stay disabled. Other fixture targets use their normal memory layout.

The later room-noise model is not the default. Neither model reliably rejects
close-up hiss or microphone handling; see the repository
[`docs/model_debug.md`](../../../../docs/model_debug.md). Fixture success and
classification accuracy do not establish continuous-stream noise rejection.
