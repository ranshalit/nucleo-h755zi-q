#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="${RUN_SCRIPTS_DIR:-${SCRIPT_DIR}}"

usage() {
  cat <<'EOF'
Usage: scripts/run.sh <app> <target> [args...]
       scripts/run.sh list|-h|--help

Run a repo application on a target via its dedicated script. Extra args are
passed unchanged (e.g. scripts/run.sh hello board --help).

Apps:    hello, iperf (zperf), cyclictest (zyclictest)
Targets: board, qemu, native_sim (native)

app \ target  board                        qemu                            native_sim
hello         flash_zephyr_hello_shell.sh  run_zephyr_hello_shell_qemu.sh  run_zephyr_hello_shell_native.sh
iperf         flash_zephyr_zperf.sh        -                               -
cyclictest    flash_zephyr_zyclictest.sh   -                               -
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

case "${1:-}" in
  list|-h|--help) usage; exit 0 ;;
esac
if [[ $# -lt 2 ]]; then
  usage >&2
  exit 2
fi

app="$1"
target="$2"
shift 2

case "$app" in
  hello) ;;
  iperf|zperf) app=iperf ;;
  cyclictest|zyclictest) app=cyclictest ;;
  *) die "unknown app: $app (use hello, iperf, cyclictest)" ;;
esac

case "$target" in
  board|qemu|native_sim) ;;
  native) target=native_sim ;;
  *) die "unknown target: $target (use board, qemu, native_sim)" ;;
esac

case "$app/$target" in
  hello/board) script=flash_zephyr_hello_shell.sh ;;
  hello/qemu) script=run_zephyr_hello_shell_qemu.sh ;;
  hello/native_sim) script=run_zephyr_hello_shell_native.sh ;;
  iperf/board) script=flash_zephyr_zperf.sh ;;
  cyclictest/board) script=flash_zephyr_zyclictest.sh ;;
  *) die "$app is not supported on $target (supported: board)" ;;
esac

exec bash "${SCRIPTS_DIR}/${script}" "$@"
