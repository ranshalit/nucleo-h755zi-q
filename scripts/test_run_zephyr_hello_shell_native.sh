#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
RUNNER="$SCRIPT_DIR/run_zephyr_hello_shell_native.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -r -- "$TMP_DIR"' EXIT

cat > "$TMP_DIR/west" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$WEST_ARGS"
if [[ "${FAKE_WEST_FAIL:-0}" != 0 ]]; then
  exit "$FAKE_WEST_FAIL"
fi
while [[ $# -gt 0 ]]; do
  if [[ "$1" == "-d" ]]; then
    shift
    if [[ "$1" == "$WEST_TEST_DIR/"* ]]; then
      mkdir -p "$1/zephyr"
      printf '#!/usr/bin/env bash\nprintf "simulator ran\\n"\n' > "$1/zephyr/zephyr.exe"
      chmod +x "$1/zephyr/zephyr.exe"
    fi
    exit 0
  fi
  shift
done
exit 9
EOF
chmod +x "$TMP_DIR/west"
export WEST_ARGS="$TMP_DIR/args"
export WEST_TEST_DIR="$TMP_DIR"

cd "$TMP_DIR"

WEST_BIN="$TMP_DIR/west" bash "$RUNNER" --build-only > "$TMP_DIR/out"
grep -Fqx -- "$REPO_ROOT/apps/zephyr-hello-shell" "$WEST_ARGS"
grep -Fqx -- "-DEXTRA_CONF_FILE=$REPO_ROOT/apps/zephyr-hello-shell/conf/native_sim_localhost.conf" "$WEST_ARGS"
grep -Fqx -- "-b" "$WEST_ARGS"
grep -Fqx -- "native_sim/native/64" "$WEST_ARGS"
grep -Fqx -- "$REPO_ROOT/build/zephyr-hello-shell-native-localhost" "$WEST_ARGS"
grep -Fq 'http://127.0.0.1:8080/' "$TMP_DIR/out"

WEST_BIN="$TMP_DIR/west" bash "$RUNNER" --profile tap --build-only --build-dir tap-build > "$TMP_DIR/out"
grep -Fqx -- "$TMP_DIR/tap-build" "$WEST_ARGS"
grep -Fqx -- "-DEXTRA_CONF_FILE=$REPO_ROOT/apps/zephyr-hello-shell/conf/native_sim_tap.conf" "$WEST_ARGS"
grep -Fq 'http://192.168.55.2/' "$TMP_DIR/out"

WEST_BIN="$TMP_DIR/west" bash "$RUNNER" --build-dir run-build > "$TMP_DIR/out"
grep -Fqx -- "$TMP_DIR/run-build" "$WEST_ARGS"
grep -Fq 'simulator ran' "$TMP_DIR/out"

rm "$WEST_ARGS"
WEST_BIN="$TMP_DIR/missing-west" bash "$RUNNER" --run-only --build-dir run-build > "$TMP_DIR/out"
grep -Fq 'simulator ran' "$TMP_DIR/out"
test ! -e "$WEST_ARGS"

check_failure() {
  local expected="$1"
  shift
  if "$@" > "$TMP_DIR/out" 2> "$TMP_DIR/err"; then
    printf 'expected failure: %s\n' "$*" >&2
    exit 1
  fi
  grep -Fq "$expected" "$TMP_DIR/err"
}

check_failure 'unknown profile' bash "$RUNNER" --profile bogus
check_failure 'requires a value' bash "$RUNNER" --profile
check_failure 'requires a value' bash "$RUNNER" --profile --build-only
check_failure 'requires a value' bash "$RUNNER" --build-dir
check_failure 'requires a value' bash "$RUNNER" --build-dir --run-only
check_failure 'cannot combine' bash "$RUNNER" --build-only --run-only
check_failure 'unknown option' bash "$RUNNER" --bogus
check_failure 'no runnable simulator' bash "$RUNNER" --run-only --build-dir missing
check_failure 'west not found' env WEST_BIN="$TMP_DIR/missing-west" bash "$RUNNER" --build-only --build-dir missing-build
check_failure 'west build failed' env WEST_BIN="$TMP_DIR/west" FAKE_WEST_FAIL=23 bash "$RUNNER" --build-only --build-dir failed-build
set +e
WEST_BIN="$TMP_DIR/west" FAKE_WEST_FAIL=23 bash "$RUNNER" --build-only --build-dir failed-build > "$TMP_DIR/out" 2> "$TMP_DIR/err"
status=$?
set -e
test "$status" -eq 23

bash "$RUNNER" --help > "$TMP_DIR/out"
grep -Fq -- '--profile' "$TMP_DIR/out"

printf 'native simulator runner tests passed\n'
