#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${ROOT}/apps/zephyr-hello-shell"
BUILD="${ROOT}/build/speech-tests"
mkdir -p "${BUILD}"
MODEL=0
case "${1:-}" in
  "") ;;
  --model) MODEL=1 ;;
  *) printf 'Usage: %s [--model]\n' "$0" >&2; exit 2 ;;
esac
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" \
  "${APP}/tests/speech/test_speech_audio.c" "${APP}/src/speech/speech_audio.c" \
  -lm -o "${BUILD}/test_audio"
"${BUILD}/test_audio"
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" -I"${APP}/src/usb_host" \
  "${APP}/tests/speech/test_speech_decision.c" "${APP}/src/speech/speech_decision.c" \
  -o "${BUILD}/test_decision"
"${BUILD}/test_decision"
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" -I"${APP}/src/usb_host" \
  "${APP}/tests/speech/test_speech_attempt.c" "${APP}/src/speech/speech_attempt.c" \
  -o "${BUILD}/test_attempt"
"${BUILD}/test_attempt"
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" -I"${APP}/src/usb_host" \
  "${APP}/tests/speech/test_speech_json.c" "${APP}/src/speech/speech_json.c" \
  -o "${BUILD}/test_json"
"${BUILD}/test_json"
if [[ "${MODEL}" == 1 ]]; then
  ZEPHYR_BASE="${ZEPHYR_BASE:-${ROOT}/vendor/zephyrproject/zephyr}"
  [[ -d "${ZEPHYR_BASE}" ]] || { printf 'error: set ZEPHYR_BASE to the shared Zephyr source\n' >&2; exit 1; }
  (
    cd "${ZEPHYR_BASE}"
    ZEPHYR_TOOLCHAIN_VARIANT=host "${WEST_BIN:-west}" build -b native_sim/native/64 \
      "${APP}/tests/speech/model" -d "${ROOT}/build/speech-model-native"
  )
  "${ROOT}/build/speech-model-native/zephyr/zephyr.exe" \
    -uart_stdinout -no-rt -stop_at=180 | tee "${BUILD}/model.log"
  grep -q 'PASS: model fixtures' "${BUILD}/model.log"
  grep -q 'PASS: worker warmup/inference/gap/disconnect/unsupported transitions' "${BUILD}/model.log"
fi
