# Native Simulator Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run the Zephyr application with one repository-relative simulator script.

**Architecture:** A standalone Bash wrapper resolves paths relative to its own file, builds a selected native_sim profile with `west`, then replaces itself with the simulator executable. The board flashing script stays untouched.

**Tech Stack:** Bash, west, Zephyr 4.2.2 `native_sim/native/64`, host gcc, curl.

## Global Constraints

- Default: build and run localhost on `http://127.0.0.1:8080/`.
- `--profile tap` uses `conf/native_sim_tap.conf` and `http://192.168.55.2/`, while localhost uses `conf/native_sim_localhost.conf`.
- `--build-only` and `--run-only` are mutually exclusive; `--build-dir PATH` overrides a per-profile `build/` directory. Resolve relative override paths from the caller's original directory.
- Locate the repository, application, and vendored Zephyr from the script directory, not `$PWD`; pass absolute paths to west (symlink-safe and merge-safe).
- Respect `WEST_BIN` when building, but do not require it for `--run-only`. Set `ZEPHYR_TOOLCHAIN_VARIANT=host` only for `west build`.
- Fail clearly on bad options/profile, missing Zephyr, west, config, or run-only executable. Propagate build/runner status and do not create/configure TAP.

---

## File map

- `scripts/run_zephyr_hello_shell_native.sh`: argument parsing, path resolution, build and foreground run.
- `scripts/test_run_zephyr_hello_shell_native.sh`: shell tests with a fake west and fake executable for argument/path/failure behavior.
- `README.md`: recommended helper usage; preserve manual west commands.

### Task 1: Build-and-run helper

**Files:**
- Create: `scripts/run_zephyr_hello_shell_native.sh`
- Create: `scripts/test_run_zephyr_hello_shell_native.sh`
- Modify: `README.md:62-117`

**Interfaces:** CLI `scripts/run_zephyr_hello_shell_native.sh [--profile localhost|tap] [--build-only|--run-only] [--build-dir PATH]`; `WEST_BIN` overrides executable.

- [ ] **Step 1: Write failing CLI tests.** Add a Bash test script that runs from `/tmp`, uses `mktemp -d`, traps cleanup, and creates an executable fake west which records arguments and creates `zephyr/zephyr.exe` in the `-d` path. Test default selects localhost and prints its URL; `--profile tap --build-only` selects TAP config and does not start the simulator; `--run-only --build-dir PATH` does not call west and runs an existing executable; relative `--build-dir` is interpreted from `/tmp`; bad profile, missing option value, missing run-only executable, missing west, and nonzero west exit return nonzero with an error on stderr. Assert recorded `-d`, `-b native_sim/native/64`, app and config arguments use absolute paths rooted in script repository, not `/tmp`. Use distinct temp paths to avoid modifying existing build outputs.

- [ ] **Step 2: Confirm test initially fails.** Run `bash scripts/test_run_zephyr_hello_shell_native.sh`; expect failure because runner does not exist.

- [ ] **Step 3: Implement wrapper.** Start with:

```bash
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
```

Parse options with a `while`/`case`, reject unknown/missing values, choose one of the two exact config files, and default build directory to `$REPO_ROOT/build/zephyr-hello-shell-native-$PROFILE`. Resolve a relative override using `"$CALLER_DIR/$value"` (canonicalize only when it exists). Check config and Zephyr directory, then run:

```bash
(
  cd "$ZEPHYR_DIR"
  ZEPHYR_TOOLCHAIN_VARIANT=host "${WEST_BIN:-west}" build -p always \
    -b native_sim/native/64 "$APP_DIR" -d "$BUILD_DIR" \
    -- "-DEXTRA_CONF_FILE=$APP_DIR/conf/native_sim_$PROFILE.conf"
)
```

Use `command -v` to diagnose absent west; `--run-only` checks executable with `-x`. Print selected profile/URL and `exec "$BUILD_DIR/zephyr/zephyr.exe"` for foreground run. Make script executable.

- [ ] **Step 4: Make tests pass; check formatting and docs.** Run `bash scripts/test_run_zephyr_hello_shell_native.sh` and `bash -n scripts/run_zephyr_hello_shell_native.sh`; update README with `scripts/run_zephyr_hello_shell_native.sh`, `--profile tap`, `--build-only`, `--run-only` examples, TAP prerequisite and URL. Preserve existing manual build commands.

- [ ] **Step 5: Real build/run smoke.** From `/tmp`, invoke helper by absolute path with `--build-only` for both profiles; run localhost binary with `--run-only`, confirm `curl --fail http://127.0.0.1:8080/api/v1/io` returns a synthetic temperature, and stop only the launched process. Run TAP binary in an isolated network namespace with a TAP interface at `192.168.55.100/24`, request `http://192.168.55.2/api/v1/io`, and leave host network untouched. Run existing host tests and the board `--build-only` regression from the feature worktree; ensure no sources or script under real-board flow changed.

- [ ] **Step 6: Commit.** Stage only runner, test, and README, then commit with trailer:

```bash
git add scripts/run_zephyr_hello_shell_native.sh scripts/test_run_zephyr_hello_shell_native.sh README.md
git commit -m "feat: add native simulator build-and-run helper" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
