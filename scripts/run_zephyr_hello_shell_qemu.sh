#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
APP_DIR="$REPO_ROOT/apps/zephyr-hello-shell"
ZEPHYR_DIR="${ZEPHYR_BASE:-$REPO_ROOT/vendor/zephyrproject/zephyr}"
BUILD_DIR="${ZEPHYR_BUILD_DIR:-$REPO_ROOT/build/zephyr-hello-shell-qemu-an500}"
WEST_BIN="${WEST_BIN:-west}"
QEMU_BIN="${QEMU_BIN:-qemu-system-arm}"
SDK_ROOT="${ZEPHYR_SDK_ROOT:-$HOME/.local/zephyr-sdk-1.0.1}"
NETWORK=user
ACTION=build-and-run
DEBUG=0

usage() {
  cat <<'EOF'
Usage: scripts/run_zephyr_hello_shell_qemu.sh [options]

Build and run zephyr-hello-shell on QEMU's MPS2 AN500 Cortex-M7 machine.

Options:
  --network user|tap   Select QEMU networking (default: user).
  --build-only         Build the firmware without running QEMU.
  --run-only           Run an existing build without rebuilding.
  --debug              Halt at startup and expose QEMU's GDB server on localhost:3333.
  --build-dir PATH     Override the Zephyr build directory.
  -h, --help           Show this help.

The user network forwards dashboard/API to localhost:8080 and MJPEG to
localhost:8081. The TAP network expects zeth configured at 192.168.55.100/24.
Set ZEPHYR_SDK_ROOT, ZEPHYR_BASE, ZEPHYR_BUILD_DIR, WEST_BIN, or QEMU_BIN to
override tool paths and locations.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --network)
      [[ $# -gt 1 && -n "$2" && "$2" != --* ]] ||
        die "--network requires user or tap"
      NETWORK="$2"
      shift 2
      ;;
    --build-only|--run-only)
      [[ "$ACTION" == build-and-run ]] ||
        die "cannot combine --build-only and --run-only"
      ACTION="${1#--}"
      shift
      ;;
    --debug)
      DEBUG=1
      shift
      ;;
    --build-dir)
      [[ $# -gt 1 && -n "$2" && "$2" != --* ]] ||
        die "--build-dir requires a path"
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

case "$NETWORK" in
  user|tap) ;;
  *) die "unknown network: $NETWORK (use user or tap)" ;;
esac

[[ -f "$ZEPHYR_DIR/CMakeLists.txt" ]] ||
  die "Zephyr workspace not found: $ZEPHYR_DIR"
[[ -f "$APP_DIR/CMakeLists.txt" ]] || die "application not found: $APP_DIR"

if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$PWD/$BUILD_DIR"
fi

if [[ "$ACTION" != run-only ]]; then
  command -v "$WEST_BIN" >/dev/null 2>&1 ||
    die "west not found: $WEST_BIN (install west or set WEST_BIN)"
  [[ -x "$SDK_ROOT/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]] ||
    die "Zephyr SDK compiler not found under $SDK_ROOT; set ZEPHYR_SDK_ROOT"

  printf '==> Building MPS2 AN500 firmware\n'
  (
    cd "$ZEPHYR_DIR"
    ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
      CROSS_COMPILE="$SDK_ROOT/arm-zephyr-eabi/bin/arm-zephyr-eabi-" \
      "$WEST_BIN" build -p always -b mps2/an500 "$APP_DIR" -d "$BUILD_DIR"
  ) || die "west build failed"
fi

[[ "$ACTION" == build-only ]] && exit 0

ELF="$BUILD_DIR/zephyr/zephyr.elf"
[[ -f "$ELF" ]] || die "firmware ELF not found: $ELF (build first)"
command -v "$QEMU_BIN" >/dev/null 2>&1 ||
  die "QEMU not found: $QEMU_BIN (install qemu-system-arm or set QEMU_BIN)"

qemu_args=(-machine mps2-an500 -display none -serial stdio -monitor none -kernel "$ELF")
if [[ "$NETWORK" == user ]]; then
  qemu_args+=(-nic 'user,net=192.168.55.0/24,host=192.168.55.100,hostfwd=tcp:127.0.0.1:8080-192.168.55.2:80,hostfwd=tcp:127.0.0.1:8081-192.168.55.2:8081')
else
  qemu_args+=(-nic tap,model=lan9118,ifname=zeth,script=no,downscript=no)
fi

if (( DEBUG )); then
  qemu_args+=(-S -gdb tcp:127.0.0.1:3333)
  printf '==> QEMU halted at reset; GDB server listening on localhost:3333\n'
fi

printf '==> Starting QEMU MPS2 AN500 (%s networking)\n' "$NETWORK"
if [[ "$NETWORK" == user ]]; then
  printf 'Dashboard: http://127.0.0.1:8080/  Camera: http://127.0.0.1:8081/\n'
else
  printf 'Dashboard: http://192.168.55.2/  Camera: http://192.168.55.2:8081/\n'
fi
exec "$QEMU_BIN" "${qemu_args[@]}"
