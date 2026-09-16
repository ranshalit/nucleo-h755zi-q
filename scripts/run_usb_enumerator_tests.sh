#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/src/usb_host"
TEST_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/tests/usb_host"
BUILD_DIR="${ZEPHYR_HOST_TEST_BUILD_DIR:-/tmp/usb-enumerator-test}"

mkdir -p "${BUILD_DIR}"

gcc -std=c99 -Wall -Wextra -Werror \
  -I"${SRC_DIR}" \
  "${TEST_DIR}/test_usb_enumerator.c" \
  "${SRC_DIR}/usb_enumerator.c" \
  -o "${BUILD_DIR}/test_usb_enumerator"

"${BUILD_DIR}/test_usb_enumerator"
