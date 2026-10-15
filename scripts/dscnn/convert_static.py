import sys, numpy as np, tensorflow as tf
sys.argv += [] ; import train_dscnn as T
src, out = sys.argv[1], sys.argv[2]
model = tf.keras.models.load_model(src)
S, Z = T.S, T.Z
tr = np.load('train_2.npz'); xtr = tr['x']
te = np.load('test_stock.npz')
fn = tf.function(lambda x: model(x, training=False)).get_concrete_function(tf.TensorSpec([1, 1960], tf.float32))
rng = np.random.default_rng(0); idx = rng.choice(len(xtr), 300, replace=False)
def rep():
    yield [np.linspace(0, 255 * S, 1960, dtype=np.float32).reshape(1, 1960)]
    for k in idx: yield [T.deq(xtr[k]).reshape(1, 1960)]
c = tf.lite.TFLiteConverter.from_concrete_functions([fn], model)
c.optimizations = [tf.lite.Optimize.DEFAULT]
c.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
c.inference_input_type = tf.int8; c.inference_output_type = tf.int8
c.representative_dataset = rep
tfl = c.convert()
it = tf.lite.Interpreter(model_content=tfl); it.allocate_tensors()
inp = it.get_input_details()[0]
print('input', inp['shape'], inp['quantization'], 'ops', sorted({o['op_name'] for o in it._get_ops_details()}))
assert abs(inp['quantization'][0] - S) < 1e-6 and inp['quantization'][1] == Z
conf = T.int8_eval(tfl, te['x'], te['y'])
print(f'int8 stock test accuracy {100*np.trace(conf)/conf.sum():.2f}%')
open(out, 'wb').write(tfl); print('wrote', out, len(tfl))
