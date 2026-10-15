"""Dump int8 micro-frontend features from the stock speech_commands pipeline.

Usage: dump_features.py OUT.npz SPLIT COUNT SEED   (SPLIT=training|validation|testing; COUNT=-1 = whole split, no augmentation)
Features are quantized exactly as the board frontend (scale 0.10171568, zero -128).
"""
import os, sys, numpy as np
sys.path.append(os.path.expanduser('~/kws-train/tf-src/tensorflow/examples/speech_commands'))
import input_data, models, tensorflow as tf
out, split, count, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
data_dir = os.environ.get('KWS_DATA', os.path.expanduser('~/kws-train/dataset_v4/'))
words = 'yes,no,up,down,left,right,stop'.split(',')
settings = models.prepare_model_settings(9, 16000, 1000, 30.0, 20, 40, 'micro')
proc = input_data.AudioProcessor('', data_dir, 11, 11, words, 10, 10, settings, '/tmp/kws-dump-logs')
np.random.seed(seed)
S, Z = 0.10171568393707275, -128
xs, ys = [], []
with tf.compat.v1.Session() as sess:
    if count < 0:
        x, y = proc.get_data(-1, 0, settings, 0.0, 0.0, 0, split, sess); xs.append(x); ys.append(y)
    else:
        for i in range(0, count, 1000):
            x, y = proc.get_data(1000, 0, settings, 0.8, 0.1, 1600, split, sess)
            xs.append(x); ys.append(y)
            print(split, seed, i + 1000, flush=True)
x = np.concatenate(xs); y = np.concatenate(ys).astype(np.int8)
q = np.clip(np.round(x / S) + Z, -128, 127).astype(np.int8)
np.savez(out, x=q, y=y)
print('saved', out, q.shape, np.bincount(y))
