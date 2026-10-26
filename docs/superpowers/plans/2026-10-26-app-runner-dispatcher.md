# App Runner Dispatcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `scripts/run.sh <app> <target> [args...]` that dispatches to the existing per-app scripts.

**Architecture:** One bash `case` table normalizes aliases, maps `app/target` to a child script, and `exec bash`es it with the remaining args. A stub-based shell test checks dispatch and errors.

**Tech Stack:** bash.

## Global Constraints

- Apps: `hello`, `iperf` (alias `zperf`), `cyclictest` (alias `zyclictest`).
- Targets: `board`, `qemu`, `native_sim` (alias `native`).
- Usage/unknown/unsupported errors exit 2; `list`/`-h`/`--help` exit 0.
- Unsupported message: `error: <app> is not supported on <target> (supported: board)`.
- `RUN_SCRIPTS_DIR` overrides the child-script directory.
- Out of scope: new qemu/native_sim support for iperf/cyclictest.

---

### Task 1: Dispatcher script with test

**Files:**
- Create: `scripts/run.sh`
- Test: `scripts/test_run.sh`

**Interfaces:**
- Produces: CLI `scripts/run.sh <app> <target> [args...]`; env `RUN_SCRIPTS_DIR`.

- [ ] **Step 1: Write the failing test** — `scripts/test_run.sh`:

```bash
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash scripts/test_run.sh`
Expected: FAIL (`scripts/run.sh` missing).

- [ ] **Step 3: Implement** — `scripts/run.sh` (chmod +x):

```bash
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash scripts/test_run.sh`
Expected: `PASS`. Also smoke: `scripts/run.sh list`, `scripts/run.sh hello qemu --help`.

- [ ] **Step 5: Document** — add to `README.md`, before the first per-app script section, a "Running applications" section with the matrix and examples:

```markdown
## Running applications

`scripts/run.sh <app> <target> [args...]` dispatches to the per-app script and
passes extra args through. `scripts/run.sh list` shows the matrix.

| app \ target | board | qemu | native_sim |
|---|---|---|---|
| `hello` (dashboard) | `flash_zephyr_hello_shell.sh` | `run_zephyr_hello_shell_qemu.sh` | `run_zephyr_hello_shell_native.sh` |
| `iperf` (`zperf`) | `flash_zephyr_zperf.sh` | — | — |
| `cyclictest` (`zyclictest`) | `flash_zephyr_zyclictest.sh` | — | — |

    scripts/run.sh hello board
    scripts/run.sh hello qemu --network tap
    scripts/run.sh hello native --profile tap
    scripts/run.sh iperf board --restore
    scripts/run.sh cyclictest board --help
```

- [ ] **Step 6: Commit**

```bash
git add scripts/run.sh scripts/test_run.sh README.md
git commit -m "feat(scripts): add run.sh app/target dispatcher"
```
