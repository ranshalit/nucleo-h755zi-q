"""Train a DS-CNN keyword model on dumped int8 features and export int8 TFLite.

Input is the board's int8 feature window (49 frames x 40 channels, scale
0.10171568, zero -128); output is the 9-way softmax in firmware label order.
"""
import argparse, os, numpy as np, tensorflow as tf

S, Z = 0.10171568393707275, -128
LABELS = ['_silence_', '_unknown_', 'yes', 'no', 'up', 'down', 'left', 'right', 'stop']


def deq(q):
    return ((q.astype(np.float32) - Z) * S)


def build(filters, blocks, stride):
    inp = tf.keras.Input((1960,), name='features')
    x = tf.keras.layers.Reshape((49, 40, 1))(inp)
    x = tf.keras.layers.Conv2D(filters, (10, 4), strides=stride, padding='same', use_bias=False)(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    for _ in range(blocks):
        x = tf.keras.layers.DepthwiseConv2D(3, padding='same', use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)
        x = tf.keras.layers.Conv2D(filters, 1, use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.AveragePooling2D(pool_size=x.shape[1:3])(x)
    x = tf.keras.layers.Flatten()(x)
    x = tf.keras.layers.Dropout(0.3)(x)
    out = tf.keras.layers.Dense(len(LABELS), activation='softmax')(x)
    return tf.keras.Model(inp, out)


def int8_eval(model_bytes, x, y):
    it = tf.lite.Interpreter(model_content=model_bytes,
                             experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    it.allocate_tensors()
    i, o = it.get_input_details()[0], it.get_output_details()[0]
    conf = np.zeros((9, 9), int)
    for q, t in zip(x, y):
        it.set_tensor(i['index'], q.reshape(1, 1960)); it.invoke()
        conf[int(t), int(np.argmax(it.get_tensor(o['index'])[0]))] += 1
    return conf


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dir', default=os.path.expanduser('~/kws-train/dscnn'))
    ap.add_argument('--filters', type=int, default=64)
    ap.add_argument('--blocks', type=int, default=4)
    ap.add_argument('--stride', type=int, nargs=2, default=[2, 2])
    ap.add_argument('--epochs', type=int, default=20)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    tr = [np.load(os.path.join(a.dir, f)) for f in sorted(os.listdir(a.dir)) if f.startswith('train_') and f.endswith('.npz')]
    xtr = np.concatenate([d['x'] for d in tr]); ytr = np.concatenate([d['y'] for d in tr]).astype(np.int32)
    va = np.load(os.path.join(a.dir, 'val.npz')); te = np.load(os.path.join(a.dir, 'test_stock.npz'))
    print('train', xtr.shape, np.bincount(ytr))
    ds = (tf.data.Dataset.from_tensor_slices((xtr, ytr)).shuffle(200000)
          .batch(256).map(lambda q, t: ((tf.cast(q, tf.float32) - Z) * S, t)).prefetch(4))
    model = build(a.filters, a.blocks, tuple(a.stride))
    model.summary(print_fn=lambda s: None)
    print('params', model.count_params())
    steps = a.epochs * int(np.ceil(len(xtr) / 256))
    lr = tf.keras.optimizers.schedules.CosineDecay(2e-3, steps, alpha=0.01)
    model.compile(tf.keras.optimizers.Adam(lr), 'sparse_categorical_crossentropy', ['accuracy'])
    model.fit(ds, epochs=a.epochs, validation_data=(deq(va['x']), va['y'].astype(np.int32)), verbose=2)
    model.save(a.out + '.keras')

    rng = np.random.default_rng(0)
    rep_idx = rng.choice(len(xtr), 300, replace=False)
    def representative():
        yield [np.linspace(0, 255 * S, 1960, dtype=np.float32).reshape(1, 1960)]
        for k in rep_idx:
            yield [deq(xtr[k]).reshape(1, 1960)]
    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    conv.representative_dataset = representative
    tfl = conv.convert()
    it = tf.lite.Interpreter(model_content=tfl); it.allocate_tensors()
    inp = it.get_input_details()[0]
    print('input', inp['shape'], inp['quantization'], 'ops', sorted({o['op_name'] for o in it._get_ops_details()}))
    if abs(inp['quantization'][0] - S) > 1e-6 or inp['quantization'][1] != Z:
        raise SystemExit('input quantization mismatch with board frontend')
    conf = int8_eval(tfl, te['x'], te['y'])
    print(f'int8 stock test accuracy {100 * np.trace(conf) / conf.sum():.2f}% (N={conf.sum()})')
    for k, n in enumerate(LABELS):
        print(f'  {n:>10}: {100 * conf[k, k] / max(1, conf[k].sum()):5.1f}%')
    open(a.out, 'wb').write(tfl)
    print('wrote', a.out, len(tfl), 'bytes')


if __name__ == '__main__':
    main()
