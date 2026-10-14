#!/usr/bin/env bash
# Retrain the on-board tiny_conv keyword model and export it as int8 TFLite.
# Uses TensorFlow's speech_commands scripts (r2.15) and Speech Commands v0.02.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${SPEECH_TRAIN_DIR:-${HOME}/kws-train}"
WORDS="${SPEECH_WORDS:-yes,no,up,down,left,right}"
STEPS="${SPEECH_STEPS:-12000,3000}"
RATES="${SPEECH_RATES:-0.001,0.0001}"
TRAIN="${SPEECH_TRAIN_SUBDIR:-train}"
CHECKPOINT="${SPEECH_START_CHECKPOINT:-}"
OUT="${ROOT}/apps/zephyr-hello-shell/models/speech_keywords.tflite"
DATA_URL="https://storage.googleapis.com/download.tensorflow.org/data/speech_commands_v0.02.tar.gz"

mkdir -p "${WORK}"
cd "${WORK}"
if [[ ! -x venv/bin/python ]]; then
  python3 -m venv venv
  venv/bin/pip install -q "tensorflow[and-cuda]==2.15.1" numpy==1.26.4
fi
if [[ ! -d tf-src/tensorflow/examples/speech_commands ]]; then
  git clone -q --depth 1 --branch r2.15 --filter=blob:none --sparse \
    https://github.com/tensorflow/tensorflow tf-src
  git -C tf-src sparse-checkout set tensorflow/examples/speech_commands
fi
if [[ ! -f dataset/validation_list.txt ]]; then
  mkdir -p dataset
  curl -sSL "${DATA_URL}" | tar xz -C dataset
fi

labels=$(( $(tr -cd ',' <<<"${WORDS}" | wc -c) + 3 ))
percent=$(( 100 / labels ))
if compgen -G 'dataset/_background_noise_/extra_*.wav' > /dev/null; then
  printf 'error: dataset contains experimental extra noise; use a clean SPEECH_TRAIN_DIR for the v1 recipe\n' >&2
  exit 1
fi
total=$(( $(tr ',' '+' <<<"${STEPS}") ))
SC="tf-src/tensorflow/examples/speech_commands"
export TF_CPP_MIN_LOG_LEVEL=2

if [[ ! -f "${TRAIN}/tiny_conv.ckpt-${total}.index" ]]; then
  venv/bin/python "${SC}/train.py" --data_url= --data_dir=dataset/ \
    --wanted_words="${WORDS}" --silence_percentage="${percent}" \
    --unknown_percentage="${percent}" --preprocess=micro --window_stride=20 \
    --model_architecture=tiny_conv --how_many_training_steps="${STEPS}" \
    --learning_rate="${RATES}" --train_dir="${TRAIN}/" --summaries_dir="logs-${TRAIN}/" \
    --start_checkpoint="${CHECKPOINT}" \
    --eval_step_interval=1000 --save_step_interval=1000
fi
SAVED_MODEL="${TRAIN}/saved_model"
venv/bin/python "${SC}/freeze.py" --wanted_words="${WORDS}" --window_stride_ms=20 \
  --preprocess=micro --model_architecture=tiny_conv \
  --start_checkpoint="${TRAIN}/tiny_conv.ckpt-${total}" --save_format=saved_model \
  --output_file="${SAVED_MODEL}"
venv/bin/python "${ROOT}/scripts/export_speech_keywords.py" --speech-commands "${SC}" \
  --data-dir dataset/ --words "${WORDS}" --saved-model "${SAVED_MODEL}" --output "${OUT}"
