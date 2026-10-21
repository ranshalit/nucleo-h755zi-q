#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
ELF="$REPO_ROOT/build/zephyr-hello-shell/zephyr/zephyr.elf"
METADATA="$REPO_ROOT/vendor/zephyrproject/zephyr/subsys/tracing/ctf/tsdl/metadata"
OUTPUT=""
RESET=0
SECONDS_ARG=""
OPENOCD_PID=""
GDB_STARTED=0
CAPTURE_DONE=0

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      cat <<'EOF'
Usage: scripts/capture_zephyr_trace.sh [options]

Capture the running STM32H755 M7's Zephyr CTF trace through ST-LINK.
Starts and stops its own OpenOCD; do not start another debug server.
Default: snapshot existing RAM recording without resetting or flashing.

  --reset          Reset board for a fresh boot recording (clears old trace).
  --seconds N      Wait N seconds after reset before capture (default: 20).
                   Requires --reset; accepts nonnegative decimal seconds.
  --elf FILE       ELF matching flashed firmware
                   (default: build/zephyr-hello-shell/zephyr/zephyr.elf).
  --output DIR     New output directory; must not already exist.
                   Default: ~/zephyr-traces/m7-<timestamp>-<unique suffix>.
  --help           Show this help.

Requires Python 3 (host checks), Zephyr GDB, and STM32CubeIDE OpenOCD.
Existing helper overrides apply: ZEPHYR_GDB, ZEPHYR_SDK_ROOT,
CROSS_COMPILE, STM32_OPENOCD, STM32_OPENOCD_SCRIPTS.
Use the Zephyr checkout/metadata matching the flashed firmware.

Capture covers only M7. The finite RAM buffer stops recording when full:
--seconds is a delay before dumping, not guaranteed recording duration.
On capture errors, a recovery session attempts resume/detach; logs remain.
EOF
      exit 0
      ;;
    --reset)
      RESET=1
      shift
      ;;
    --elf|--output|--seconds)
      [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || fail "$1 requires a value"
      case "$1" in
        --elf) ELF="$2" ;;
        --output) OUTPUT="$2" ;;
        --seconds) SECONDS_ARG="$2" ;;
      esac
      shift 2
      ;;
    *) fail "unknown option: $1 (see --help)" ;;
  esac
done

if [[ -n "$SECONDS_ARG" ]]; then
  [[ "$RESET" == 1 ]] || fail "--seconds requires --reset"
  [[ "$SECONDS_ARG" =~ ^[0-9]+([.][0-9]+)?$ ]] || fail "--seconds must be nonnegative decimal seconds"
else
  SECONDS_ARG=20
fi
[[ -f "$ELF" ]] || fail "ELF not found: $ELF"
[[ -f "$METADATA" ]] || fail "CTF metadata not found: $METADATA"
command -v python3 >/dev/null || fail "python3 not found"

# Never attach to another session's server or stop a process we did not start.
python3 - <<'PY'
import socket
import sys

with socket.socket() as sock:
    try:
        sock.bind(("127.0.0.1", 3333))
    except OSError as error:
        sys.exit(f"error: cannot reserve GDB port 3333; stop existing debugger first: {error}")
PY

if [[ -z "$OUTPUT" ]]; then
  mkdir -p "$HOME/zephyr-traces"
  OUTPUT="$(mktemp -d "$HOME/zephyr-traces/m7-$(date +%Y%m%d-%H%M%S)-XXXXXX")"
else
  [[ ! -e "$OUTPUT" ]] || fail "output already exists: $OUTPUT"
  mkdir -p -- "$(dirname -- "$OUTPUT")"
  mkdir -- "$OUTPUT"
fi
OUTPUT="$(cd -- "$OUTPUT" && pwd -P)"
[[ "$OUTPUT" != *$'\n'* && "$OUTPUT" != *$'\r'* ]] || fail "output path must not contain newlines"
LOG_DIR="$OUTPUT/.capture"
mkdir "$LOG_DIR"
ELF="$(cd -- "$(dirname -- "$ELF")" && pwd -P)/$(basename -- "$ELF")"
cp -- "$METADATA" "$OUTPUT/metadata"
printf 'ELF: %s\nReset: %s\nWait seconds (reset only): %s\n' \
  "$ELF" "$RESET" "$SECONDS_ARG" > "$LOG_DIR/capture-info.txt"

cleanup() {
  local status=$?
  trap - EXIT
  if [[ "$GDB_STARTED" == 1 && "$CAPTURE_DONE" == 0 ]]; then
    printf 'Capture interrupted/failed; attempting to resume M7.\n' >&2
    if ! "$SCRIPT_DIR/gdb_zephyr_hello_shell.sh" "$ELF" -nx -batch \
      -ex 'set remotetimeout 5' \
      -ex 'target extended-remote localhost:3333' \
      -ex 'monitor resume' -ex 'detach' > "$LOG_DIR/recovery.log" 2>&1; then
      printf 'error: resume recovery failed; inspect board state and %s/recovery.log\n' "$LOG_DIR" >&2
      cat "$LOG_DIR/recovery.log" >&2
    fi
  fi
  if [[ -n "$OPENOCD_PID" ]]; then
    if kill -0 "$OPENOCD_PID" 2>/dev/null; then
      kill "$OPENOCD_PID"
    fi
    wait "$OPENOCD_PID" || true
  fi
  if [[ "$status" != 0 ]]; then
    printf 'error: capture failed; logs: %s\n' "$LOG_DIR" >&2
  fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if ! "$SCRIPT_DIR/gdb_zephyr_hello_shell.sh" "$ELF" -nx -batch \
  -ex 'p sizeof(ram_tracing)' -ex "p &'tracing_backend_ram.c'::pos" \
  > "$LOG_DIR/preflight.log" 2>&1; then
  cat "$LOG_DIR/preflight.log" >&2
  fail "GDB/ELF preflight failed; require matching ELF with RAM tracing symbols"
fi

cat > "$LOG_DIR/capture.gdb" <<GDB
set pagination off
set confirm off
set remotetimeout 10
target extended-remote localhost:3333
set \$trace_reset = $RESET
if \$trace_reset
monitor reset halt
monitor resume
shell sleep $SECONDS_ARG
end
monitor halt
set \$trace_pos = 'tracing_backend_ram.c'::pos
if \$trace_pos <= 0 || \$trace_pos > sizeof(ram_tracing)
echo error: invalid trace position; check matching ELF, tracing configuration, and capture delay.\\n
quit 1
end
dump binary memory channel0_0 ram_tracing (ram_tracing + \$trace_pos)
printf "Captured %u/%u bytes\\n", \$trace_pos, sizeof(ram_tracing)
monitor resume
detach
GDB

OPENOCD_ARGS=()
if [[ "$RESET" == 1 ]]; then
  printf 'Reset requested: clears existing recording; waiting %s seconds after boot.\n' "$SECONDS_ARG"
  OPENOCD_ARGS+=(--connect-under-reset)
fi
"$SCRIPT_DIR/debug_zephyr_hello_shell.sh" "${OPENOCD_ARGS[@]}" \
  > "$LOG_DIR/openocd.log" 2>&1 &
OPENOCD_PID=$!

READY=0
for ((attempt = 0; attempt < 100; attempt++)); do
  if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then
    cat "$LOG_DIR/openocd.log" >&2
    fail "OpenOCD exited before ready; snapshot attach failures may require explicit --reset"
  fi
  if grep -q 'Listening on port 3333 for gdb connections' "$LOG_DIR/openocd.log"; then
    READY=1
    break
  fi
  sleep 0.1
done
if [[ "$READY" != 1 ]]; then
  cat "$LOG_DIR/openocd.log" >&2
  fail "timed out waiting for OpenOCD GDB port 3333"
fi

GDB_STARTED=1
if ! (cd -- "$OUTPUT" && "$SCRIPT_DIR/gdb_zephyr_hello_shell.sh" "$ELF" \
  -nx -batch -x "$LOG_DIR/capture.gdb") > "$LOG_DIR/gdb.log" 2>&1; then
  cat "$LOG_DIR/gdb.log" >&2
  fail "GDB capture failed; inspect board state if resume/detach also failed"
fi
[[ -s "$OUTPUT/channel0_0" ]] || fail "GDB produced no trace data"
CAPTURE_DONE=1
cat "$LOG_DIR/gdb.log"
printf '\nCapture saved: %s\nImport this directory as a CTF trace in Trace Compass.\n' "$OUTPUT"
