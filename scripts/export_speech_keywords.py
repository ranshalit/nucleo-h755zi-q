#!/usr/bin/env python3
"""Convert a frozen tiny_conv SavedModel to the int8 model used on the STM32."""
import argparse
import sys

import numpy as np
import tensorflow as tf

SAMPLE_RATE = 16000
CLIP_MS = 1000
WINDOW_MS = 30.0
STRIDE_MS = 20
FEATURES = 40
# The on-board frontend emits int8 features on this fixed scale (0..25.94).
INPUT_SCALE = 0.10171568393707275
INPUT_ZERO = -128
ALLOWED_OPS = {"CONV_2D", "FULLY_CONNECTED", "RESHAPE", "SOFTMAX"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--speech-commands", required=True)
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--words", required=True)
    ap.add_argument("--saved-model", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    sys.path.append(args.speech_commands)
    import input_data  # pylint: disable=import-outside-toplevel
    import models  # pylint: disable=import-outside-toplevel

    words = args.words.split(",")
    settings = models.prepare_model_settings(
        len(input_data.prepare_words_list(words)), SAMPLE_RATE, CLIP_MS,
        WINDOW_MS, STRIDE_MS, FEATURES, "micro")
    percent = int(100 / (len(words) + 2))
    processor = input_data.AudioProcessor(
        "", args.data_dir, percent, percent, words, 10, 10, settings, "logs/")
    size = settings["fingerprint_size"]

    with tf.compat.v1.Session() as sess:
        rep = [processor.get_data(1, i, settings, 0.8, 0.1, 100.0, "testing", sess)[0]
               for i in range(100)]
        test_x, test_y = processor.get_data(-1, 0, settings, 0.0, 0.0, 0.0, "testing", sess)

    lo = INPUT_SCALE * (-128 - INPUT_ZERO)
    hi = INPUT_SCALE * (127 - INPUT_ZERO)

    def representative():
        # Pin the calibrated input range to the frontend's fixed int8 scale.
        yield [np.linspace(lo, hi, size, dtype=np.float32).reshape(1, size)]
        for data in rep:
            yield [np.clip(data, lo, hi).astype(np.float32).reshape(1, size)]

    converter = tf.lite.TFLiteConverter.from_saved_model(args.saved_model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    converter.representative_dataset = representative
    model = converter.convert()

    interp = tf.lite.Interpreter(
        model_content=model,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]
    ops = {op["op_name"] for op in interp._get_ops_details()}  # pylint: disable=protected-access
    scale, zero = inp["quantization"]
    print(f"input {inp['shape']} scale={scale:.8f} zero={zero}; output {out['shape']}")
    print(f"ops {sorted(ops)}")
    if abs(scale - INPUT_SCALE) > 1e-6 or zero != INPUT_ZERO:
        print("error: input quantization does not match the on-board frontend")
        return 1
    if not ops <= ALLOWED_OPS or out["shape"][-1] != len(words) + 2:
        print("error: unexpected ops or label count")
        return 1

    labels = input_data.prepare_words_list(words)
    confusion = np.zeros((len(labels), len(labels)), dtype=int)
    for x, y in zip(test_x, test_y):
        q = np.clip(np.round(x / scale + zero), -128, 127).astype(np.int8)
        interp.set_tensor(inp["index"], q.reshape(1, size))
        interp.invoke()
        confusion[int(y), int(np.argmax(interp.get_tensor(out["index"])[0]))] += 1
    accuracy = np.trace(confusion) / confusion.sum()
    print(f"int8 test accuracy {accuracy * 100:.2f}% (N={confusion.sum()})")
    for i, name in enumerate(labels):
        print(f"  {name:>10}: {confusion[i, i] / max(1, confusion[i].sum()) * 100:5.1f}%")

    with open(args.output, "wb") as f:
        f.write(model)
    print(f"wrote {args.output} ({len(model)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
