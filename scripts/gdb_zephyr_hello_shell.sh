#!/usr/bin/env bash

set -euo pipefail

if [[ -n "${ZEPHYR_GDB:-}" ]]; then
  GDB="${ZEPHYR_GDB}"
elif [[ -n "${CROSS_COMPILE:-}" ]]; then
  GDB="${CROSS_COMPILE}gdb"
elif [[ -n "${ZEPHYR_SDK_ROOT:-}" ]]; then
  GDB="${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
else
  GDB=""
  for candidate in "${HOME}/.local/zephyr-sdk-"*/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb; do
    if [[ -x "${candidate}" ]]; then
      GDB="${candidate}"
      break
    fi
  done
fi

if [[ -z "${GDB}" || ! -x "${GDB}" ]]; then
  printf 'error: Zephyr GDB not found; set ZEPHYR_GDB or ZEPHYR_SDK_ROOT\n' >&2
  exit 1
fi

exec "${GDB}" "$@"
