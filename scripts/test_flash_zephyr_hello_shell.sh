#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
RUNNER="$SCRIPT_DIR/flash_zephyr_hello_shell.sh"
WORK_DIR="$REPO_ROOT/build/test-flash-guard"

rm -rf -- "$WORK_DIR"
mkdir -p "$WORK_DIR/bin" "$WORK_DIR/zephyr-base" "$WORK_DIR/app" "$WORK_DIR/build/zephyr"
trap 'rm -rf -- "$WORK_DIR"' EXIT

cat > "$WORK_DIR/bin/west" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >> "$WEST_ARGS"
if [[ "${1:-}" == build && -f "$TEST_BUILD_DIR/CMakeCache.txt" ]]; then
  if ! grep -Fqx -- "ZEPHYR_BASE:PATH=$TEST_ZEPHYR_BASE" "$TEST_BUILD_DIR/CMakeCache.txt"; then
    echo "stale CMake cache reached west build" >&2
    exit 1
  fi
fi
exit 0
EOF
chmod +x "$WORK_DIR/bin/west"

cat > "$WORK_DIR/bin/STM32_Programmer_CLI" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$PROGRAMMER_ARGS"
case "${FAKE_BCM4_MODE:-zero}" in
  zero)
    printf '\033[39;49m\033[39;49m\033[0m     BCM4 : 0x0 (CM4 boot disabled)\n'
    ;;
  one)
    printf '\033[39;49m\033[39;49m\033[0m     BCM4 : 0x1 (CM4 boot enabled)\n'
    ;;
  bad)
    echo "OPTION BYTES BANK: unavailable"
    ;;
  fail)
    echo "target not connected"
    exit 9
    ;;
  *)
    echo "unknown FAKE_BCM4_MODE"
    exit 2
    ;;
esac
EOF
chmod +x "$WORK_DIR/bin/STM32_Programmer_CLI"

printf ':020000040800F2\n:00000001FF\n' > "$WORK_DIR/build/zephyr/zephyr.hex"

check_failure() {
  local expected="$1"
  shift
  if "$@" >"$WORK_DIR/out" 2>"$WORK_DIR/err"; then
    printf 'expected failure: %s\n' "$*" >&2
    exit 1
  fi
  grep -Fq "$expected" "$WORK_DIR/err"
}

COMMON_ARGS=(
  --flash-only
  --skip-validate
  --zephyr-base "$WORK_DIR/zephyr-base"
  --app-dir "$WORK_DIR/app"
  --build-dir "$WORK_DIR/build"
  --programmer "$WORK_DIR/bin/STM32_Programmer_CLI"
  --freq 1234
)
FLASH_WITH_VALIDATION_ARGS=(
  --flash-only
  --zephyr-base "$WORK_DIR/zephyr-base"
  --app-dir "$WORK_DIR/app"
  --build-dir "$WORK_DIR/build"
  --programmer "$WORK_DIR/bin/STM32_Programmer_CLI"
  --freq 1234
)

export WEST_ARGS="$WORK_DIR/west-args"
export PROGRAMMER_ARGS="$WORK_DIR/programmer-args"

cat > "$WORK_DIR/bin/python3" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$WORK_DIR/bin/python3"

rm -f "$WEST_ARGS" "$PROGRAMMER_ARGS"
check_failure 'BCM4 option byte must be 0' env PATH="$WORK_DIR/bin:$PATH" WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=one bash "$RUNNER" "${FLASH_WITH_VALIDATION_ARGS[@]}"
test ! -e "$WEST_ARGS"

rm -f "$WEST_ARGS" "$PROGRAMMER_ARGS"
check_failure 'BCM4 option byte must be 0' env WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=one bash "$RUNNER" "${COMMON_ARGS[@]}"
grep -Fqx -- 'port=SWD' "$PROGRAMMER_ARGS"
grep -Fqx -- 'freq=1234' "$PROGRAMMER_ARGS"
grep -Fqx -- '-ob' "$PROGRAMMER_ARGS"
grep -Fqx -- 'displ' "$PROGRAMMER_ARGS"
grep -Fqx -- 'mode=UR' "$PROGRAMMER_ARGS"
test ! -e "$WEST_ARGS"

rm -f "$WEST_ARGS"
check_failure 'unable to parse BCM4 option byte' env WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=bad bash "$RUNNER" "${COMMON_ARGS[@]}"
test ! -e "$WEST_ARGS"

rm -f "$WEST_ARGS"
check_failure 'failed to read option bytes' env WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=fail bash "$RUNNER" "${COMMON_ARGS[@]}"
test ! -e "$WEST_ARGS"

rm -f "$WEST_ARGS"
env WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=zero bash "$RUNNER" "${COMMON_ARGS[@]}" >"$WORK_DIR/out" 2>"$WORK_DIR/err"
grep -Fqx -- 'flash' "$WEST_ARGS"
grep -Fqx -- '--runner' "$WEST_ARGS"
grep -Fqx -- 'stm32cubeprogrammer' "$WEST_ARGS"
grep -Fqx -- '--conn-modifiers' "$WEST_ARGS"
grep -Fqx -- 'mode=UR' "$WEST_ARGS"

printf 'flash BCM4 guard tests passed\n'

export TEST_BUILD_DIR="$WORK_DIR/build"
export TEST_ZEPHYR_BASE="$WORK_DIR/zephyr-base"
BUILD_ARGS=(
  --build-only
  --zephyr-base "$WORK_DIR/zephyr-base"
  --app-dir "$WORK_DIR/app"
  --build-dir "$TEST_BUILD_DIR"
)

mkdir -p "$WORK_DIR/zephyr-base/cmake"
cat > "$WORK_DIR/zephyr-base/cmake/pristine.cmake" <<'EOF'
if(NOT IS_ABSOLUTE "${BINARY_DIR}" OR NOT IS_ABSOLUTE "${SOURCE_DIR}")
  message(FATAL_ERROR "Expected absolute build and source paths")
endif()
file(REMOVE "${BINARY_DIR}/CMakeCache.txt" "${BINARY_DIR}/stale-build-artifact")
EOF

cat > "$TEST_BUILD_DIR/CMakeCache.txt" <<EOF
ZEPHYR_BASE:PATH=$WORK_DIR/removed-worktree/zephyr
APPLICATION_SOURCE_DIR:PATH=$WORK_DIR/app
EOF
touch "$TEST_BUILD_DIR/stale-build-artifact"
rm -f "$WEST_ARGS"
if ! env WEST_BIN="$WORK_DIR/bin/west" ZEPHYR_TOOLCHAIN_VARIANT=host \
  bash "$RUNNER" "${BUILD_ARGS[@]}" >"$WORK_DIR/out" 2>"$WORK_DIR/err"; then
  cat "$WORK_DIR/out" "$WORK_DIR/err" >&2
  exit 1
fi
test ! -e "$TEST_BUILD_DIR/stale-build-artifact"
grep -Fqx -- 'build' "$WEST_ARGS"

rm -f "$WEST_ARGS"
env WEST_BIN="$WORK_DIR/bin/west" ZEPHYR_TOOLCHAIN_VARIANT=host \
  bash "$RUNNER" "${BUILD_ARGS[@]}" >"$WORK_DIR/out" 2>"$WORK_DIR/err"
grep -Fqx -- 'build' "$WEST_ARGS"

cat > "$WORK_DIR/zephyr-base/cmake/pristine.cmake" <<'EOF'
message(FATAL_ERROR "pristine failed")
EOF
cat > "$TEST_BUILD_DIR/CMakeCache.txt" <<EOF
ZEPHYR_BASE:PATH=$TEST_ZEPHYR_BASE
EOF
env WEST_BIN="$WORK_DIR/bin/west" ZEPHYR_TOOLCHAIN_VARIANT=host \
  bash "$RUNNER" "${BUILD_ARGS[@]}" >"$WORK_DIR/out" 2>"$WORK_DIR/err"

cat > "$TEST_BUILD_DIR/CMakeCache.txt" <<EOF
ZEPHYR_BASE:PATH=$WORK_DIR/removed-worktree/zephyr
EOF
rm -f "$WEST_ARGS"
check_failure 'pristine failed' env WEST_BIN="$WORK_DIR/bin/west" \
  ZEPHYR_TOOLCHAIN_VARIANT=host bash "$RUNNER" "${BUILD_ARGS[@]}"
test ! -e "$WEST_ARGS"

printf 'stale build cache recovery tests passed\n'
