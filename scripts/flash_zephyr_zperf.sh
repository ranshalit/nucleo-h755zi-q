#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR_BASE="${ZEPHYR_BASE:-${REPO_ROOT}/vendor/zephyrproject/zephyr}"
BUILD_DIR="${ZPERF_BUILD_DIR:-${REPO_ROOT}/build/zephyr-bandwidth}"
DASHBOARD_BUILD_DIR="${ZPERF_DASHBOARD_BUILD_DIR:-${ZEPHYR_BUILD_DIR:-/tmp/stm32h7-zephyr-hello-shell}}"
BACKUP_DIR="${ZPERF_BACKUP_DIR:-${REPO_ROOT}/build/zperf-dashboard-backup}"
WEST_BIN="${WEST_BIN:-west}"
ACTION="${1:---build-flash}"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

if [[ "${ACTION}" == "--help" || "${ACTION}" == "-h" ]]; then
  cat <<'EOF'
Usage: bash scripts/flash_zephyr_zperf.sh [--build-only|--flash-only|--restore]

Default: build and flash upstream Zephyr zperf, replacing the dashboard temporarily.
Before flashing, save the dashboard build's HEX once; --restore flashes that copy.
The dashboard build must match the currently running firmware.
Dashboard build: ZPERF_DASHBOARD_BUILD_DIR, then ZEPHYR_BUILD_DIR,
otherwise /tmp/stm32h7-zephyr-hello-shell (same default as the hello runner).
No UART/application validation or automatic benchmark is performed.

Board: 192.168.55.2/24. Host: 192.168.55.101/24. Use iperf v2 (not iperf3).
UART commands to receive host traffic:
  zperf tcp download 5001
  zperf udp download 5001
Host TCP test:
  iperf -c 192.168.55.2 -B 192.168.55.101 -t 10 -f m
Board TCP transmit test (start `iperf -s -B 192.168.55.101` on host first):
  zperf tcp upload 192.168.55.101 5001 10 1064

Overrides: ZPERF_BUILD_DIR, ZPERF_DASHBOARD_BUILD_DIR, ZPERF_BACKUP_DIR,
ZEPHYR_BASE, ZEPHYR_SDK_ROOT, WEST_BIN, STM32_PROGRAMMER_CLI.
EOF
  exit 0
fi
[[ $# -le 1 ]] || die "expected at most one action; use --help"
case "${ACTION}" in
  --build-flash|--build-only|--flash-only|--restore) ;;
  *) die "unknown action: ${ACTION}; use --help" ;;
esac

if [[ "${ACTION}" == "--restore" ]]; then
  [[ -s "${BACKUP_DIR}/zephyr/zephyr.hex" ]] || die "no saved dashboard HEX in ${BACKUP_DIR}"
  PROGRAMMER="${STM32_PROGRAMMER_CLI:-}"
  if [[ -z "${PROGRAMMER}" ]]; then
    for candidate in /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin/STM32_Programmer_CLI; do
      if [[ -x "${candidate}" ]]; then
        PROGRAMMER="${candidate}"
        break
      fi
    done
  fi
  [[ -x "${PROGRAMMER}" ]] || die "STM32_Programmer_CLI not found"
  exec "${PROGRAMMER}" -c port=SWD mode=UR "freq=${STM32_PROGRAMMER_SWD_FREQ:-8000}" \
    -w "${BACKUP_DIR}/zephyr/zephyr.hex" -v -rst
fi

if [[ "${ACTION}" != "--flash-only" ]]; then
  SDK="${ZEPHYR_SDK_ROOT:-}"
  if [[ -z "${SDK}" ]]; then
    for candidate in "${HOME}"/.local/zephyr-sdk-*; do
      if [[ -x "${candidate}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]; then
        SDK="${candidate}"
        break
      fi
    done
  fi
  [[ -x "${SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]] || die "Zephyr SDK ARM toolchain not found; set ZEPHYR_SDK_ROOT"
  # Module upgrades can replace parsed Kconfig files with directories.
  kconfig_sources="${BUILD_DIR}/zephyr/kconfig/sources.txt"
  if [[ -f "${kconfig_sources}" ]]; then
    while IFS= read -r source || [[ -n "${source}" ]]; do
      if [[ ! -f "${source}" ]]; then
        printf 'Discarding stale Kconfig source list: %s\n' "${source}"
        rm -- "${kconfig_sources}"
        break
      fi
    done < "${kconfig_sources}"
  fi
  (
    cd "${ZEPHYR_BASE}"
    ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
    CROSS_COMPILE="${SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-" \
      "${WEST_BIN}" build -b nucleo_h755zi_q/stm32h755xx/m7 \
      samples/net/zperf -d "${BUILD_DIR}" -- \
      "-DEXTRA_CONF_FILE=${SCRIPT_DIR}/zperf/benchmark.conf" \
      "-DDTC_OVERLAY_FILE=${SCRIPT_DIR}/zperf/benchmark.overlay"
  )
fi

if [[ "${ACTION}" != "--build-only" ]]; then
  [[ -s "${BUILD_DIR}/zephyr/zephyr.hex" ]] || die "benchmark HEX missing; build first"
  if [[ ! -e "${BACKUP_DIR}/zephyr/zephyr.hex" ]]; then
    [[ -s "${DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" ]] || die "dashboard HEX missing; cannot preserve firmware: ${DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex (set ZPERF_DASHBOARD_BUILD_DIR to the dashboard build)"
    mkdir -p "${BACKUP_DIR}/zephyr"
    cp -- "${DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" "${BACKUP_DIR}/zephyr/zephyr.hex"
  fi
  "${SCRIPT_DIR}/flash_zephyr_hello_shell.sh" --flash-only --skip-validate --build-dir "${BUILD_DIR}"
  printf '\nRestore dashboard: bash scripts/flash_zephyr_zperf.sh --restore\n'
fi
