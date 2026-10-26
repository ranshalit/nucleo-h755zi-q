#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
export ZEPHYR_BASE="${WORK_DIR}/zephyr"
export ZEPHYR_SDK_ROOT="${WORK_DIR}/sdk"
export ZPERF_BUILD_DIR="${WORK_DIR}/build"
export WEST_BIN="${WORK_DIR}/west"
export TEST_WORK_DIR="${WORK_DIR}"
mkdir -p "${ZEPHYR_BASE}" "${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin" \
  "${ZPERF_BUILD_DIR}/zephyr/kconfig" "${WORK_DIR}/TraceRecorder/config/Kconfig"
touch "${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
chmod +x "${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
printf 'config TEST\n\tbool\n' > "${WORK_DIR}/Kconfig"

cat > "${WORK_DIR}/check.cmake" <<'EOF'
if(EXISTS "$ENV{ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt")
  file(STRINGS "$ENV{ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt" sources)
  foreach(source ${sources})
    if(EXISTS "${source}")
      file(MD5 "${source}" checksum)
    endif()
  endforeach()
endif()
EOF
cat > "${WEST_BIN}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == flash ]]; then exit 0; fi
[[ "$1" == build ]]
printf '%s\n' "$@" > "${TEST_WORK_DIR}/args"
cmake -P "${TEST_WORK_DIR}/check.cmake"
printf '%s\n' "${TEST_WORK_DIR}/Kconfig" > "${ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt"
exit "${TEST_BUILD_STATUS:-0}"
EOF
chmod +x "${WEST_BIN}"

run() {
  bash "${SCRIPT_DIR}/flash_zephyr_zperf.sh" --build-only \
    > "${WORK_DIR}/out" 2> "${WORK_DIR}/err"
}
fail() { cat "${WORK_DIR}/out" "${WORK_DIR}/err" >&2; printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# Reproduce a Zephyr upgrade turning a previously parsed file into a directory.
printf '%s\n' "${WORK_DIR}/TraceRecorder/config/Kconfig" \
  > "${ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt"
run || fail "stale Kconfig directory still reaches CMake MD5"
grep -Fq 'stale Kconfig' "${WORK_DIR}/out" || fail "missing recovery notice"
grep -Fxq 'samples/net/zperf' "${WORK_DIR}/args"
grep -Fxq -- "-DEXTRA_CONF_FILE=${SCRIPT_DIR}/zperf/benchmark.conf" "${WORK_DIR}/args"
grep -Fxq -- "-DDTC_OVERLAY_FILE=${SCRIPT_DIR}/zperf/benchmark.overlay" "${WORK_DIR}/args"

run || fail "valid incremental build failed"
if grep -Fq 'stale Kconfig' "${WORK_DIR}/out"; then
  fail "valid Kconfig list invalidated"
fi

printf '%s\n' "${WORK_DIR}/removed/Kconfig" \
  > "${ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt"
run || fail "missing Kconfig recovery failed"
grep -Fq 'stale Kconfig' "${WORK_DIR}/out" || fail "missing file not detected"

rm -- "${ZPERF_BUILD_DIR}/zephyr/kconfig/sources.txt"
run || fail "fresh build failed"
export TEST_BUILD_STATUS=9
status=0
run || status=$?
[[ "${status}" == 9 ]] || fail "build failure status not propagated"

cat > "${WORK_DIR}/programmer" <<'EOF'
#!/usr/bin/env bash
printf 'BCM4 : 0x0 (CM4 boot disabled)\n'
EOF
chmod +x "${WORK_DIR}/programmer"
export STM32_PROGRAMMER_CLI="${WORK_DIR}/programmer"
export ZEPHYR_BUILD_DIR="${WORK_DIR}/dashboard"
export ZPERF_BACKUP_DIR="${WORK_DIR}/backup"
mkdir -p "${ZEPHYR_BUILD_DIR}/zephyr"
printf 'benchmark\n' > "${ZPERF_BUILD_DIR}/zephyr/zephyr.hex"
printf 'dashboard\n' > "${ZEPHYR_BUILD_DIR}/zephyr/zephyr.hex"
bash "${SCRIPT_DIR}/flash_zephyr_zperf.sh" --flash-only \
  > "${WORK_DIR}/out" 2> "${WORK_DIR}/err" || fail "shared dashboard build not used"
cmp "${ZEPHYR_BUILD_DIR}/zephyr/zephyr.hex" "${ZPERF_BACKUP_DIR}/zephyr/zephyr.hex"
export ZPERF_DASHBOARD_BUILD_DIR="${WORK_DIR}/explicit-dashboard"
export ZPERF_BACKUP_DIR="${WORK_DIR}/explicit-backup"
mkdir -p "${ZPERF_DASHBOARD_BUILD_DIR}/zephyr"
printf 'explicit dashboard\n' > "${ZPERF_DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex"
bash "${SCRIPT_DIR}/flash_zephyr_zperf.sh" --flash-only \
  > "${WORK_DIR}/out" 2> "${WORK_DIR}/err" || fail "explicit dashboard override failed"
cmp "${ZPERF_DASHBOARD_BUILD_DIR}/zephyr/zephyr.hex" "${ZPERF_BACKUP_DIR}/zephyr/zephyr.hex"

printf 'zperf stale Kconfig recovery tests passed\n'
