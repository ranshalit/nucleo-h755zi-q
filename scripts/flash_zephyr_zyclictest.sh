#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR_BASE="${ZEPHYR_BASE:-${REPO_ROOT}/vendor/zephyrproject/zephyr}"
BUILD_DIR="${ZYCLICTEST_BUILD_DIR:-${REPO_ROOT}/build/zephyr-zyclictest}"
DASHBOARD_BUILD_DIR="${ZYCLICTEST_DASHBOARD_BUILD_DIR:-${REPO_ROOT}/build/zephyr-hello-shell}"
BACKUP_DIR="${ZYCLICTEST_BACKUP_DIR:-${REPO_ROOT}/build/zyclictest-dashboard-backup}"
WEST_BIN="${WEST_BIN:-west}"
ACTION="${1:---build-flash}"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

if [[ "${ACTION}" == "--help" || "${ACTION}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/flash_zephyr_zyclictest.sh [--build-only|--flash-only|--restore]

Default: build and flash upstream Zephyr shell with zyclictest, replacing the dashboard.
Before flashing, save the dashboard build's HEX once; --restore flashes that copy.
The dashboard build must match the currently running firmware.
No UART/application validation or automatic benchmark is performed.

Target: nucleo_h755zi_q/stm32h755xx/m7. UART: ST-LINK VCP, 115200 baud.
This measures the standalone baseline, not latency under dashboard load.
Run on the UART shell (25000 cycles at 400 us takes approximately 10 seconds):
  zyclictest start -i 400 -p -11 -l 25000
Wait for "Finished thread with count: 25000", then print the summary:
  zyclictest stop -q
For histogram output, use `zyclictest stop` instead of `zyclictest stop -q`.
To cancel a loop-mode run early: zyclictest stop -c -q
Always stop the previous run before starting another.

Overrides: ZYCLICTEST_BUILD_DIR, ZYCLICTEST_DASHBOARD_BUILD_DIR,
ZYCLICTEST_BACKUP_DIR, ZEPHYR_BASE, ZEPHYR_SDK_ROOT, WEST_BIN,
STM32_PROGRAMMER_CLI, STM32_PROGRAMMER_SWD_FREQ.
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

ZEPHYR_BASE="$(cd -- "${ZEPHYR_BASE}" && pwd -P)"
export ZEPHYR_BASE

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
  (
    cd "${ZEPHYR_BASE}"
    ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
    CROSS_COMPILE="${SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-" \
      "${WEST_BIN}" build -b nucleo_h755zi_q/stm32h755xx/m7 \
      samples/subsys/shell/shell_module -d "${BUILD_DIR}" -- \
      "-DEXTRA_CONF_FILE=${SCRIPT_DIR}/zyclictest/benchmark.conf"
  )
fi

if [[ "${ACTION}" != "--build-only" ]]; then
  [[ -s "${BUILD_DIR}/zephyr/zephyr.hex" ]] || die "benchmark HEX missing; build first"
  if [[ ! -e "${BACKUP_DIR}/zephyr/zephyr.hex" ]]; then
    [[ -s "${DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" ]] || die "dashboard HEX missing; cannot preserve firmware"
    mkdir -p "${BACKUP_DIR}/zephyr"
    cp -- "${DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" "${BACKUP_DIR}/zephyr/zephyr.hex"
  fi
  [[ -s "${BACKUP_DIR}/zephyr/zephyr.hex" ]] || die "saved dashboard HEX is empty; cannot preserve firmware"
  "${SCRIPT_DIR}/flash_zephyr_hello_shell.sh" --flash-only --skip-validate --build-dir "${BUILD_DIR}"
  printf '\nRun on UART: zyclictest start -i 400 -p -11 -l 25000\n'
  printf 'After completion: zyclictest stop -q\n'
  printf 'Restore dashboard: scripts/flash_zephyr_zyclictest.sh --restore\n'
fi
