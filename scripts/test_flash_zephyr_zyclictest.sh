#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/flash_zephyr_zyclictest.sh"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "${WORK_DIR}"/{bin,zephyr-base,sdk/arm-zephyr-eabi/bin,dashboard/zephyr}

cat > "${WORK_DIR}/bin/west" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$@" >> "${WEST_ARGS}"
if [[ "$1" == build ]]; then
  [[ "${ZEPHYR_BASE}" == "$(pwd -P)" ]]
  [[ "${ZEPHYR_TOOLCHAIN_VARIANT}" == cross-compile ]]
  [[ "${CROSS_COMPILE}" == "${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-" ]]
  [[ "${FAKE_BUILD_FAIL:-0}" == 0 ]] || exit 9
  while [[ "$1" != -d ]]; do shift; done
  mkdir -p "$2/zephyr"
  printf 'benchmark\n' > "$2/zephyr/zephyr.hex"
fi
EOF
cat > "${WORK_DIR}/bin/programmer" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >> "${PROGRAMMER_ARGS}"
printf 'BCM4 : 0x0 (CM4 boot disabled)\n'
exit "${FAKE_PROGRAMMER_STATUS:-0}"
EOF
touch "${WORK_DIR}/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
chmod +x "${WORK_DIR}/bin/"* "${WORK_DIR}/sdk/arm-zephyr-eabi/bin/"*
printf 'dashboard\n' > "${WORK_DIR}/dashboard/zephyr/zephyr.hex"

ln -s "${WORK_DIR}/zephyr-base" "${WORK_DIR}/zephyr-link"
export ZEPHYR_BASE="${WORK_DIR}/zephyr-link"
export ZEPHYR_SDK_ROOT="${WORK_DIR}/sdk"
export ZYCLICTEST_BUILD_DIR="${WORK_DIR}/build"
export ZYCLICTEST_DASHBOARD_BUILD_DIR="${WORK_DIR}/dashboard"
export ZYCLICTEST_BACKUP_DIR="${WORK_DIR}/backup"
export WEST_BIN="${WORK_DIR}/bin/west"
export STM32_PROGRAMMER_CLI="${WORK_DIR}/bin/programmer"
export WEST_ARGS="${WORK_DIR}/west-args"
export PROGRAMMER_ARGS="${WORK_DIR}/programmer-args"

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
run() { bash "${RUNNER}" "$@" >"${WORK_DIR}/out" 2>"${WORK_DIR}/err"; }
check_failure() {
  local expected="$1"
  shift
  if run "$@"; then fail "expected failure: $*"; fi
  grep -Fq -- "${expected}" "${WORK_DIR}/err" || fail "missing error: ${expected}"
}

[[ -f "${RUNNER}" ]] || fail "zyclictest helper missing"
run --help
grep -Fq -- 'zyclictest start' "${WORK_DIR}/out"
[[ ! -e "${WEST_ARGS}" && ! -e "${PROGRAMMER_ARGS}" ]]
check_failure 'unknown action' --invalid
check_failure 'at most one action' --build-only unexpected
check_failure 'no saved dashboard HEX' --restore
check_failure 'benchmark HEX missing' --flash-only

run --build-only
grep -Fxq 'nucleo_h755zi_q/stm32h755xx/m7' "${WEST_ARGS}"
grep -Fxq 'samples/subsys/shell/shell_module' "${WEST_ARGS}"
grep -Fxq -- "-DEXTRA_CONF_FILE=${SCRIPT_DIR}/zyclictest/benchmark.conf" "${WEST_ARGS}"
[[ ! -e "${PROGRAMMER_ARGS}" && ! -e "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex" ]]

run --flash-only
cmp "${ZYCLICTEST_DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex"
grep -Fxq 'flash' "${WEST_ARGS}"
grep -Fxq -- '-ob' "${PROGRAMMER_ARGS}"
printf 'new dashboard\n' > "${ZYCLICTEST_DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex"
run --flash-only
grep -Fxq 'dashboard' "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex"

run --restore
grep -Fxq -- "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex" "${PROGRAMMER_ARGS}"
grep -Fxq -- '-v' "${PROGRAMMER_ARGS}"
grep -Fxq -- '-rst' "${PROGRAMMER_ARGS}"
export FAKE_PROGRAMMER_STATUS=7
if run --restore; then fail "restore ignored programmer failure"; fi
unset FAKE_PROGRAMMER_STATUS

rm -f "${WEST_ARGS}" "${PROGRAMMER_ARGS}"
run
grep -Fxq 'build' "${WEST_ARGS}"
grep -Fxq 'flash' "${WEST_ARGS}"
rm -f "${WEST_ARGS}" "${PROGRAMMER_ARGS}"
export FAKE_BUILD_FAIL=1
if run; then fail "ignored build failure"; fi
[[ ! -e "${PROGRAMMER_ARGS}" ]]
unset FAKE_BUILD_FAIL

export ZEPHYR_BUILD_DIR="${WORK_DIR}/shared-dashboard"
mkdir -p "${ZEPHYR_BUILD_DIR}/zephyr"
printf 'shared dashboard\n' > "${ZEPHYR_BUILD_DIR}/zephyr/zephyr.hex"
export ZYCLICTEST_BACKUP_DIR="${WORK_DIR}/explicit-backup"
run --flash-only
cmp "${ZYCLICTEST_DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex"
unset ZYCLICTEST_DASHBOARD_BUILD_DIR
export ZYCLICTEST_BACKUP_DIR="${WORK_DIR}/shared-backup"
run --flash-only || {
  cat "${WORK_DIR}/err" >&2
  fail "shared ZEPHYR_BUILD_DIR not used for dashboard backup"
}
cmp "${ZEPHYR_BUILD_DIR}/zephyr/zephyr.hex" "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex"
unset ZEPHYR_BUILD_DIR
rm -f "${WEST_ARGS}" "${PROGRAMMER_ARGS}"

export ZYCLICTEST_BACKUP_DIR="${WORK_DIR}/missing-backup"
export ZYCLICTEST_DASHBOARD_BUILD_DIR="${WORK_DIR}/missing-dashboard"
check_failure 'dashboard HEX missing' --flash-only
[[ ! -e "${PROGRAMMER_ARGS}" ]]
mkdir -p "${ZYCLICTEST_BACKUP_DIR}/zephyr"
touch "${ZYCLICTEST_BACKUP_DIR}/zephyr/zephyr.hex"
check_failure 'saved dashboard HEX is empty' --flash-only
[[ ! -e "${PROGRAMMER_ARGS}" ]]
check_failure 'no saved dashboard HEX' --restore
export ZEPHYR_SDK_ROOT="${WORK_DIR}/missing-sdk"
check_failure 'Zephyr SDK ARM toolchain not found' --build-only

printf 'zyclictest helper tests passed\n'
