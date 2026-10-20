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
exit 0
EOF
chmod +x "$WORK_DIR/bin/west"

cat > "$WORK_DIR/bin/STM32_Programmer_CLI" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$PROGRAMMER_ARGS"
case "${FAKE_BCM4_MODE:-zero}" in
  zero)
    cat <<'OUT'
OPTION BYTES BANK:
BCM4 : 0x0 (CM4 boot disabled)
OUT
    ;;
  one)
    cat <<'OUT'
OPTION BYTES BANK:
BCM4 : 0x1 (CM4 boot enabled)
OUT
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

export WEST_ARGS="$WORK_DIR/west-args"
export PROGRAMMER_ARGS="$WORK_DIR/programmer-args"
rm -f "$WEST_ARGS" "$PROGRAMMER_ARGS"
check_failure 'BCM4 option byte must be 0' env WEST_BIN="$WORK_DIR/bin/west" FAKE_BCM4_MODE=one bash "$RUNNER" "${COMMON_ARGS[@]}"
grep -Fqx -- 'port=SWD' "$PROGRAMMER_ARGS"
grep -Fqx -- 'freq=1234' "$PROGRAMMER_ARGS"
grep -Fqx -- '-ob' "$PROGRAMMER_ARGS"
grep -Fqx -- 'displ' "$PROGRAMMER_ARGS"
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

printf 'flash BCM4 guard tests passed\n'
