#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/src"
TEST_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/tests/camera"
BUILD_DIR="${ZEPHYR_HOST_TEST_BUILD_DIR:-/tmp/mjpeg-multipart-test}"

mkdir -p "${BUILD_DIR}"

gcc -std=c99 -Wall -Wextra -Werror \
  -I"${SRC_DIR}" \
  "${TEST_DIR}/test_mjpeg_multipart.c" \
  "${SRC_DIR}/mjpeg_multipart.c" \
  -o "${BUILD_DIR}/test_mjpeg_multipart"

"${BUILD_DIR}/test_mjpeg_multipart"
