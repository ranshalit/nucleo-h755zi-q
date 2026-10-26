#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run.sh"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT

for s in flash_zephyr_hello_shell.sh run_zephyr_hello_shell_qemu.sh \
         run_zephyr_hello_shell_native.sh flash_zephyr_zperf.sh \
         flash_zephyr_zyclictest.sh; do
  printf '#!/usr/bin/env bash\nprintf "%%s|%%s\\n" "%s" "$*"\n' "$s" > "${WORK_DIR}/$s"
done
export RUN_SCRIPTS_DIR="$WORK_DIR"

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

expect_dispatch() {
  local expected="$1"; shift
  local out
  out="$(bash "$RUNNER" "$@")" || fail "nonzero exit: $*"
  [[ "$out" == "$expected" ]] || fail "$*: got '$out', want '$expected'"
}

expect_error() {
  local msg="$1"; shift
  local status=0
  bash "$RUNNER" "$@" >"${WORK_DIR}/out" 2>"${WORK_DIR}/err" || status=$?
  [[ "$status" == 2 ]] || fail "$*: exit $status, want 2"
  grep -qF -- "$msg" "${WORK_DIR}/err" || fail "$*: stderr missing '$msg'"
}

expect_dispatch "flash_zephyr_hello_shell.sh|" hello board
expect_dispatch "run_zephyr_hello_shell_qemu.sh|--network tap" hello qemu --network tap
expect_dispatch "run_zephyr_hello_shell_native.sh|" hello native_sim
expect_dispatch "run_zephyr_hello_shell_native.sh|--profile tap" hello native --profile tap
expect_dispatch "flash_zephyr_zperf.sh|--restore" iperf board --restore
expect_dispatch "flash_zephyr_zperf.sh|" zperf board
expect_dispatch "flash_zephyr_zyclictest.sh|--build-only" cyclictest board --build-only
expect_dispatch "flash_zephyr_zyclictest.sh|" zyclictest board
expect_dispatch "flash_zephyr_hello_shell.sh|--help" hello board --help

for arg in list -h --help; do
  out="$(bash "$RUNNER" "$arg")" || fail "$arg exit nonzero"
  grep -qF "Usage:" <<<"$out" || fail "$arg: no usage"
  grep -qF "flash_zephyr_zperf.sh" <<<"$out" || fail "$arg: no matrix"
done

expect_error "Usage:"
expect_error "Usage:" hello
expect_error "unknown app: foo" foo board
expect_error "unknown target: bar" hello bar
expect_error "iperf is not supported on qemu (supported: board)" iperf qemu
expect_error "cyclictest is not supported on native_sim (supported: board)" cyclictest native

printf 'PASS\n'
