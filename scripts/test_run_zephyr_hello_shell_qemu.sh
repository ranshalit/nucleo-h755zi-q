#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
RUNNER="$SCRIPT_DIR/run_zephyr_hello_shell_qemu.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/bin" "$TMP_DIR/zephyr/cmake" \
  "$TMP_DIR/sdk/arm-zephyr-eabi/bin" "$TMP_DIR/build"
touch "$TMP_DIR/zephyr/CMakeLists.txt" "$TMP_DIR/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
chmod +x "$TMP_DIR/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"

cat > "$TMP_DIR/bin/west" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$WEST_ARGS"
if [[ -f "$TEST_BUILD_CACHE" ]]; then
  echo "stale CMake cache reached west build" >&2
  exit 1
fi
EOF
cat > "$TMP_DIR/bin/cmake" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$CMAKE_ARGS"
[[ "$*" == *"-P $TEST_ZEPHYR_BASE/cmake/pristine.cmake"* ]]
rm -f "$TEST_BUILD_CACHE"
EOF
chmod +x "$TMP_DIR/bin/west" "$TMP_DIR/bin/cmake"

export WEST_ARGS="$TMP_DIR/west-args"
export CMAKE_ARGS="$TMP_DIR/cmake-args"
export TEST_BUILD_CACHE="$TMP_DIR/build/CMakeCache.txt"
export TEST_ZEPHYR_BASE="$TMP_DIR/zephyr"
cat > "$TEST_BUILD_CACHE" <<EOF
ZEPHYR_BASE:PATH=$TMP_DIR/removed-zephyr
APPLICATION_SOURCE_DIR:PATH=$REPO_ROOT/apps/zephyr-hello-shell
EOF

PATH="$TMP_DIR/bin:$PATH" ZEPHYR_BASE="$TEST_ZEPHYR_BASE" \
  ZEPHYR_SDK_ROOT="$TMP_DIR/sdk" WEST_BIN="$TMP_DIR/bin/west" \
  bash "$RUNNER" --build-only --build-dir "$TMP_DIR/build"

grep -Fq -- "$TEST_ZEPHYR_BASE/cmake/pristine.cmake" "$CMAKE_ARGS"
test ! -e "$TEST_BUILD_CACHE"
grep -Fqx -- 'build' "$WEST_ARGS"

printf 'QEMU runner stale-cache recovery tests passed\n'
