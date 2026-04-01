#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PROGRAMMER="${STM32_PROGRAMMER_CLI:-/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI}"
PORT="${ZEPHYR_UART_PORT:-}"
SWD_FREQ="${STM32_PROGRAMMER_SWD_FREQ:-8000}"
SAMPLES="${ZEPHYR_BOOT_MEASURE_SAMPLES:-5}"
PROMPT_TIMEOUT_SEC="${ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC:-15}"
JSON_OUTPUT=0
VERBOSE_OUTPUT=0
RELEASE_PORT_HOLDERS=1

usage() {
  cat <<'EOF'
Usage: scripts/measure_zephyr_boot_time.sh [options]

Measure Zephyr reset-to-prompt time on the attached board.

This script assumes the target image is already flashed. It excludes programming
time from the measurement and instead times repeated hardware resets:

  host timer start -> STM32CubeProgrammer -hardRst -> serial prompt usable

It then immediately runs `kernel uptime` over the serial shell. That command
executes on the board, so it provides the device-side uptime when the prompt
became usable.

Before measuring, the script will terminate same-user processes that already
hold the UART port (for example `minicom`). If the port is still busy after
that, the script exits with a clear error.

Options:
  --samples COUNT       Number of reset cycles to measure. Default: 5.
  --timeout-sec SEC     Per-run timeout waiting for the prompt. Default: 15.
  --port PATH           Override the UART device path.
  --programmer PATH     Override the STM32_Programmer_CLI path.
  --freq KHZ            Override the SWD frequency in kHz. Default: 8000.
  --json                Emit JSON instead of human-readable output.
  --verbose             Include per-run transcript snippets in output.
  --no-release-port     Do not terminate same-user UART holder processes.
  -h, --help            Show this help text.

Environment overrides:
  ZEPHYR_UART_PORT
  STM32_PROGRAMMER_CLI
  STM32_PROGRAMMER_SWD_FREQ
  ZEPHYR_BOOT_MEASURE_SAMPLES
  ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '==> %s\n' "$*"
}

detect_port() {
  if [[ -n "${PORT}" ]]; then
    return
  fi

  local candidate=""
  for candidate in \
    /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*if02 \
    /dev/serial/by-id/*STLINK*if02 \
    /dev/ttyACM0; do
    if [[ -e "${candidate}" ]]; then
      PORT="${candidate}"
      return
    fi
  done

  die "unable to auto-detect a Zephyr UART port; pass --port"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples)
      shift
      [[ $# -gt 0 ]] || die "--samples requires a value"
      SAMPLES="$1"
      ;;
    --timeout-sec)
      shift
      [[ $# -gt 0 ]] || die "--timeout-sec requires a value"
      PROMPT_TIMEOUT_SEC="$1"
      ;;
    --port)
      shift
      [[ $# -gt 0 ]] || die "--port requires a value"
      PORT="$1"
      ;;
    --programmer)
      shift
      [[ $# -gt 0 ]] || die "--programmer requires a value"
      PROGRAMMER="$1"
      ;;
    --freq)
      shift
      [[ $# -gt 0 ]] || die "--freq requires a value"
      SWD_FREQ="$1"
      ;;
    --json)
      JSON_OUTPUT=1
      ;;
    --verbose)
      VERBOSE_OUTPUT=1
      ;;
    --no-release-port)
      RELEASE_PORT_HOLDERS=0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

[[ "${SAMPLES}" =~ ^[1-9][0-9]*$ ]] || die "--samples must be a positive integer"
[[ "${PROMPT_TIMEOUT_SEC}" =~ ^([0-9]+([.][0-9]+)?|[.][0-9]+)$ ]] || die "--timeout-sec must be a positive number"
[[ -x "${PROGRAMMER}" ]] || die "STM32_Programmer_CLI not found: ${PROGRAMMER}"
command -v python3 >/dev/null 2>&1 || die "python3 not found"

detect_port

if [[ "${JSON_OUTPUT}" -eq 0 ]]; then
  log "measuring boot time on ${PORT}"
  log "image must already be flashed; programming time is excluded"
fi

ZEPHYR_UART_PORT="${PORT}" \
STM32_PROGRAMMER_CLI="${PROGRAMMER}" \
STM32_PROGRAMMER_SWD_FREQ="${SWD_FREQ}" \
ZEPHYR_BOOT_MEASURE_SAMPLES="${SAMPLES}" \
ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC="${PROMPT_TIMEOUT_SEC}" \
ZEPHYR_BOOT_MEASURE_JSON="${JSON_OUTPUT}" \
ZEPHYR_BOOT_MEASURE_VERBOSE="${VERBOSE_OUTPUT}" \
ZEPHYR_BOOT_MEASURE_RELEASE_PORT="${RELEASE_PORT_HOLDERS}" \
python3 "${SCRIPT_DIR}/measure_zephyr_boot_time.py"
