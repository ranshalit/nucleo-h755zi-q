#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${ROOT}/apps/zephyr-hello-shell"
BUILD="${ROOT}/build/speech-tests"
mkdir -p "${BUILD}"
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" \
  "${APP}/tests/speech/test_speech_audio.c" "${APP}/src/speech/speech_audio.c" \
  -lm -o "${BUILD}/test_audio"
"${BUILD}/test_audio"
