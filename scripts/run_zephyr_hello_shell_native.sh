#!/usr/bin/env bash
set -euo pipefail

CALLER_DIR="$PWD"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
APP_DIR="$REPO_ROOT/apps/zephyr-hello-shell"
ZEPHYR_DIR="$REPO_ROOT/vendor/zephyrproject/zephyr"
PROFILE=localhost
BUILD=1
RUN=1
BUILD_DIR=""
ACTION=""

usage() {
  cat <<'EOF'
Usage: scripts/run_zephyr_hello_shell_native.sh [options]

Build and run the Zephyr application on native_sim (localhost by default).

  --profile localhost|tap   Select networking profile (default: localhost).
  --build-only              Build without running.
  --run-only                Run an existing build without rebuilding.
  --build-dir PATH          Override the per-profile build directory.
  -h, --help                Show this help.

Set WEST_BIN to use a different west executable. The TAP profile requires a
separately configured host TAP interface at 192.168.55.100/24.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      [[ $# -gt 1 && -n "$2" && "$2" != --* ]] ||
        die "--profile requires a value"
      PROFILE="$2"
      shift 2
      ;;
    --build-only|--run-only)
      [[ -z "$ACTION" ]] || die "cannot combine --build-only and --run-only"
      ACTION="$1"
      shift
      ;;
    --build-dir)
      [[ $# -gt 1 && -n "$2" && "$2" != --* ]] ||
        die "--build-dir requires a value"
      BUILD_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

case "$PROFILE" in
  localhost)
    URL="http://127.0.0.1:8080/"
    ;;
  tap)
    URL="http://192.168.55.2/"
    ;;
  *)
    die "unknown profile: $PROFILE (use localhost or tap)"
    ;;
esac

case "$ACTION" in
  --build-only) RUN=0 ;;
  --run-only) BUILD=0 ;;
esac

if [[ -z "$BUILD_DIR" ]]; then
  BUILD_DIR="$REPO_ROOT/build/zephyr-hello-shell-native-$PROFILE"
elif [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$CALLER_DIR/$BUILD_DIR"
fi
BUILD_DIR="$(realpath -m -- "$BUILD_DIR")"

[[ -f "$ZEPHYR_DIR/CMakeLists.txt" ]] || die "Zephyr workspace not found: $ZEPHYR_DIR"
[[ -f "$APP_DIR/conf/native_sim_$PROFILE.conf" ]] ||
  die "native_sim profile not found: $APP_DIR/conf/native_sim_$PROFILE.conf"

printf 'Dashboard: %s\n' "$URL"
if [[ "$PROFILE" == tap ]]; then
  printf 'TAP requires a preconfigured host interface at 192.168.55.100/24.\n'
fi

if (( BUILD )); then
  command -v "${WEST_BIN:-west}" >/dev/null 2>&1 ||
    die "west not found: ${WEST_BIN:-west} (install west or set WEST_BIN)"
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cached_zephyr_base="$(sed -n 's/^ZEPHYR_BASE:PATH=//p' "$BUILD_DIR/CMakeCache.txt")"
    if [[ -n "$cached_zephyr_base" && "$cached_zephyr_base" != "$ZEPHYR_DIR" ]]; then
      printf 'Cleaning build cache from previous Zephyr tree: %s\n' "$cached_zephyr_base"
      cmake "-DBINARY_DIR=$(cd -- "$BUILD_DIR" && pwd -P)" \
        "-DSOURCE_DIR=$APP_DIR" -P "$ZEPHYR_DIR/cmake/pristine.cmake"
    fi
  fi
  if (
    cd "$ZEPHYR_DIR"
    ZEPHYR_TOOLCHAIN_VARIANT=host "${WEST_BIN:-west}" build -p always \
      -b native_sim/native/64 "$APP_DIR" -d "$BUILD_DIR" \
      -- "-DEXTRA_CONF_FILE=$APP_DIR/conf/native_sim_$PROFILE.conf"
  ); then
    :
  else
    status=$?
    printf 'error: west build failed (exit %d)\n' "$status" >&2
    exit "$status"
  fi
fi

if (( RUN )); then
  executable="$BUILD_DIR/zephyr/zephyr.exe"
  [[ -x "$executable" ]] || die "no runnable simulator at $executable; build first"
  exec "$executable"
fi
