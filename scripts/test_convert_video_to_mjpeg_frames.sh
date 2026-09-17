#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONVERTER="${SCRIPT_DIR}/convert_video_to_mjpeg_frames.py"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "==> generating synthetic test input video"
ffmpeg -y -f lavfi -i testsrc=size=64x64:rate=10 -t 2 -pix_fmt yuv420p \
  "${WORK_DIR}/test-input.mp4" >/dev/null 2>&1

echo "==> running converter with a generous flash budget (expect success)"
OUT_DIR="${WORK_DIR}/out-ok"
mkdir -p "${OUT_DIR}"
OUTPUT="$(python3 "${CONVERTER}" --input "${WORK_DIR}/test-input.mp4" \
  --output-dir "${OUT_DIR}" --width 32 --height 24 --fps 4 --duration 1 \
  --var-prefix test_frame --flash-budget-bytes 1000000)"
echo "${OUTPUT}"

EXPECTED_FRAME_COUNT="$(echo "${OUTPUT}" | grep -oE 'Generated [0-9]+ frames' | grep -oE '[0-9]+')"

[[ -f "${OUT_DIR}/camera_frames.c" ]] || { echo "FAIL: camera_frames.c missing"; exit 1; }
[[ -f "${OUT_DIR}/camera_frames.h" ]] || { echo "FAIL: camera_frames.h missing"; exit 1; }

echo "==> checking generated source compiles"
gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only \
  -I"${OUT_DIR}" "${OUT_DIR}/camera_frames.c"

echo "==> checking generated frame count matches converter output"
cat > "${WORK_DIR}/count_main.c" <<'MAIN_EOF'
#include <stdio.h>
#include "camera_frames.h"

int main(void)
{
	printf("%zu\n", camera_frame_count);
	return 0;
}
MAIN_EOF

gcc -std=c99 -Wall -Wextra -Werror -I"${OUT_DIR}" \
  "${WORK_DIR}/count_main.c" "${OUT_DIR}/camera_frames.c" \
  -o "${WORK_DIR}/count_main"
ACTUAL_FRAME_COUNT="$("${WORK_DIR}/count_main")"

if [[ "${ACTUAL_FRAME_COUNT}" != "${EXPECTED_FRAME_COUNT}" ]]; then
  echo "FAIL: camera_frame_count (${ACTUAL_FRAME_COUNT}) != converter-reported count (${EXPECTED_FRAME_COUNT})"
  exit 1
fi

echo "==> running converter with a tiny flash budget (expect failure)"
OUT_DIR_FAIL="${WORK_DIR}/out-fail"
mkdir -p "${OUT_DIR_FAIL}"
if python3 "${CONVERTER}" --input "${WORK_DIR}/test-input.mp4" \
  --output-dir "${OUT_DIR_FAIL}" --width 32 --height 24 --fps 4 --duration 1 \
  --var-prefix test_frame --flash-budget-bytes 10 2>"${WORK_DIR}/stderr.txt"; then
  echo "FAIL: converter should have exited non-zero for an impossible flash budget"
  exit 1
fi

grep -q "exceeds flash budget" "${WORK_DIR}/stderr.txt" || {
  echo "FAIL: expected 'exceeds flash budget' error message"
  cat "${WORK_DIR}/stderr.txt"
  exit 1
}

echo "ALL TESTS PASSED"
