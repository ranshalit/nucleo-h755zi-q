# Zephyr 4.4.0 Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the repository's Zephyr workspace to v4.4.0, expose `zyclictest` on the physical M7 target, and preserve Zephyr-dependent script behavior.

**Architecture:** Build from a clean v4.4.0 west workspace staged inside this worktree's ignored `build/` directory. Verify the M7 configuration and primary build before moving the staged workspace into this worktree's established `vendor/zephyrproject` path. Leave the original checkout's v4.2.2 workspace untouched as rollback. Reapply archived local patches only where upstream has not already incorporated their fixes.

**Tech Stack:** Zephyr v4.4.0, west, CMake/Ninja, Zephyr SDK `arm-zephyr-eabi`, Bash and Python regression scripts.

## Global Constraints

- Upgrade to exactly Zephyr v4.4.0; do not upgrade beyond v4.4.0.
- Keep `zyclictest` enabled only for `nucleo_h755zi_q/stm32h755xx/m7`.
- Set `CONFIG_TICKLESS_KERNEL=y` and `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000` for the M7 configuration.
- Preserve the current v4.2.2 workspace and do not discard or overwrite local Zephyr changes.
- Keep both archived patch files unchanged; apply either patch only if the v4.4.0 source still needs it.
- Align the app's TFLite Micro submodule with the Zephyr v4.4.0 integration when the v4.1 pin lacks sources required by Zephyr v4.4.0.
- Preserve existing Zephyr script arguments, environment overrides, output locations, and expected artifacts.
- Do not modify unrelated scripts or flash hardware without explicit user authorization.

---

### Task 1: Stage and inspect the v4.4.0 west workspace

**Files:**
- Create ignored workspace: `build/zephyrproject-v4.4.0-stage/`
- Read only: `patches/udc_stm32-sof-enable.patch`
- Read only: `patches/stm32h7-hcd-rx-before-hcint.patch`

**Interfaces:**
- Consumes: Zephyr v4.4.0 upstream west manifest.
- Produces: A clean staged workspace whose `zephyr` project is exactly v4.4.0 and whose dependencies match that manifest.

- [ ] **Step 1: Initialize the staged workspace at the selected release**

Run from the repository root:

```bash
west init \
  -m https://github.com/zephyrproject-rtos/zephyr \
  --mr v4.4.0 \
  build/zephyrproject-v4.4.0-stage
```

Expected: west creates a new workspace without changing `vendor/zephyrproject`.

- [ ] **Step 2: Fetch manifest dependencies**

```bash
cd build/zephyrproject-v4.4.0-stage/zephyr
west update
```

Expected: the Zephyr project and required manifest dependencies check out successfully.

- [ ] **Step 3: Verify the selected Zephyr revision**

```bash
git describe --tags --exact-match
git status --short
```

Expected: the tag is `v4.4.0` and the Zephyr source tree is clean before patch inspection.

- [ ] **Step 4: Check whether the UDC SOF fix is already upstream**

Inspect `drivers/usb/udc/udc_stm32.c` for the `Sof_enable` assignment in the PCD initialization function. If the assignment is absent, check and apply the archived patch:

```bash
git -C build/zephyrproject-v4.4.0-stage/zephyr \
  apply --check "$PWD/patches/udc_stm32-sof-enable.patch"
git -C build/zephyrproject-v4.4.0-stage/zephyr \
  apply "$PWD/patches/udc_stm32-sof-enable.patch"
```

Expected: the v4.4.0 tree either already contains the fix or the archived patch applies cleanly. Do not force-apply it.

- [ ] **Step 5: Check whether the STM32H7 HAL HCD fix is still needed**

Inspect the v4.4.0 manifest-selected STM32 HAL source at
`modules/hal/stm32/stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c`.
Compare `HAL_HCD_IRQHandler()` with `patches/stm32h7-hcd-rx-before-hcint.patch`.
The v4.4.0 source still needs the ordering fix, but its changed context makes
`git apply --check` fail. Do not modify the upstream module source. The app's
existing `cmake/patch_stm32_hcd.cmake` validates and applies the archived patch
to a private build copy; confirm that this path succeeds as part of the M7
build and that the app CMake checks verify RX draining precedes HCINT handling.

### Task 2: Enable zyclictest and build the M7 application

**Files:**
- Modify: `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`
- Modify gitlink: `apps/zephyr-hello-shell/third_party/tflite-micro`
- Build output: `/tmp/stm32h7-zephyr-4.4-m7/`

**Interfaces:**
- Consumes: `build/zephyrproject-v4.4.0-stage/zephyr` and the existing app build script.
- Produces: M7 firmware with the zyclictest shell command and the documented timing resolution.

- [ ] **Step 1: Add M7-only Kconfig selections**

Add these selections to `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`:

```conf
CONFIG_ZYCLICTEST_SHELL=y
CONFIG_TICKLESS_KERNEL=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000
```

Do not add them to `prj.conf`, `native_sim`, or `mps2_an500` configuration.

- [ ] **Step 2: Align the TFLite Micro submodule with the Zephyr 4.4 integration**

The current app submodule revision `8d404de73acf7687831e16d88e86e4f73cfddf8e` is on Zephyr's `zephyr-v4.1.0` branch and lacks `tensorflow/lite/micro/kernels/decode.cc`, which the Zephyr v4.4.0 module CMakeLists requires. Check out the exact Zephyr v4.4.0 branch commit:

```bash
git -C apps/zephyr-hello-shell/third_party/tflite-micro \
  checkout --detach fcc760af130f3a595b5802cdebcc77461e54f382
test -f apps/zephyr-hello-shell/third_party/tflite-micro/tensorflow/lite/micro/kernels/decode.cc
```

Expected: the file exists and the parent repository reports only the intended submodule gitlink change.

- [ ] **Step 3: Build using the staged Zephyr tree**

From the repository root:

Run the command with the Python 3.12 environment preceding the system Python and with `WEST_BIN` pointing at the same environment's `west` executable:

```bash
PATH="$PWD/build/python312-venv/bin:$PATH" \
WEST_BIN="$PWD/build/python312-venv/bin/west" \
bash scripts/flash_zephyr_hello_shell.sh \
  --build-only \
  --zephyr-base "$PWD/build/zephyrproject-v4.4.0-stage/zephyr" \
  --build-dir /tmp/stm32h7-zephyr-4.4-m7
```

Expected: the Nucleo M7 image builds successfully with the Zephyr SDK.

- [ ] **Step 4: Verify resolved M7 configuration**

```bash
grep -E '^CONFIG_(ZYCLICTEST_SHELL|TICKLESS_KERNEL|SYS_CLOCK_TICKS_PER_SEC)=' \
  /tmp/stm32h7-zephyr-4.4-m7/zephyr/.config
```

Expected output includes:

```text
CONFIG_ZYCLICTEST_SHELL=y
CONFIG_TICKLESS_KERNEL=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000
```

If Kconfig rejects 1 MHz or the STM32H755 timer configuration cannot support it, stop before promotion and report the concrete build/configuration error.

### Task 3: Promote the staged workspace and update version documentation

**Files:**
- Move staged workspace to: `vendor/zephyrproject/`
- Preserve the original checkout workspace at `../vendor/zephyrproject/` without modification.
- Modify: `README.md`
- Modify: `summary/zephyr.md`

**Interfaces:**
- Consumes: The verified staged v4.4.0 workspace and successful M7 build.
- Produces: This worktree's standard `vendor/zephyrproject` path points to v4.4.0; the original checkout's v4.2.2 workspace remains untouched for rollback.

- [ ] **Step 1: Preserve the original workspace without deleting it**

Confirm the staged workspace and M7 build exist. Then from this worktree root:

```bash
mv build/zephyrproject-v4.4.0-stage \
  vendor/zephyrproject
```

Expected: this worktree's `vendor/zephyrproject/zephyr` is v4.4.0. The v4.2.2 workspace in the original checkout remains untouched with its existing local change intact.

- [ ] **Step 2: Update Zephyr release references and setup commands**

In `README.md` and `summary/zephyr.md`, replace the old v4.2.2 pin/banner examples with v4.4.0 and update workspace initialization instructions to select `--mr v4.4.0`. Retain the established `vendor/zephyrproject/zephyr` source path and existing board/toolchain guidance.

- [ ] **Step 3: Check documentation diff**

```bash
git diff --check
rg -n 'v4\.2\.2|--mr v4\.2\.2' README.md summary/zephyr.md
```

Expected: no whitespace errors and no stale active v4.2.2 build instructions. Historical notes, if any, must remain clearly identified as historical.

### Task 4: Audit Zephyr script compatibility and run regression checks

**Files:**
- Review and modify only if a v4.4.0 incompatibility is demonstrated:
  - `scripts/flash_zephyr_hello_shell.sh`
  - `scripts/flash_zephyr_zperf.sh`
  - `scripts/run_zephyr_hello_shell_native.sh`
  - `scripts/run_zephyr_hello_shell_qemu.sh`
  - Zephyr trace/debug helpers and their tests
- Run existing tests:
  - `scripts/test_flash_zephyr_hello_shell.sh`
  - `scripts/test_run_zephyr_hello_shell_native.sh`
  - `scripts/test_capture_zephyr_trace.py`

**Interfaces:**
- Consumes: The promoted v4.4.0 workspace and existing script interfaces.
- Produces: Passing regression tests and supported build outputs, with targeted compatibility fixes only where required.

- [ ] **Step 1: Run existing script regression tests**

```bash
bash scripts/test_flash_zephyr_hello_shell.sh
bash scripts/test_run_zephyr_hello_shell_native.sh
python3 scripts/test_capture_zephyr_trace.py
```

Expected: all three commands exit successfully. If one fails, identify whether v4.4.0 changed the consumed interface/artifact and make the smallest relevant fix before continuing.

- [ ] **Step 2: Check Bash syntax and help interfaces**

Run `bash -n` on the Zephyr-related Bash scripts under `scripts/`, then run `--help` for `flash_zephyr_hello_shell.sh`, `flash_zephyr_zperf.sh`, `run_zephyr_hello_shell_native.sh`, and `run_zephyr_hello_shell_qemu.sh`.

Expected: no syntax errors, and existing command names/options remain available.

- [ ] **Step 3: Build all supported Zephyr profiles**

```bash
bash scripts/flash_zephyr_hello_shell.sh --build-only
bash scripts/run_zephyr_hello_shell_native.sh --profile localhost --build-only
bash scripts/run_zephyr_hello_shell_native.sh --profile tap --build-only
bash scripts/run_zephyr_hello_shell_qemu.sh --build-only
bash scripts/flash_zephyr_zperf.sh --build-only
```

Expected: all existing profiles and the standalone zperf sample build. Confirm simulator `.config` files do not enable `CONFIG_ZYCLICTEST_SHELL`.

- [ ] **Step 4: Review the final tracked diff and workspace state**

```bash
git diff --check
git status --short
```

Expected: only intended app configuration and documentation/script compatibility changes appear in the repository diff. The promoted v4.4.0 workspace is ignored; the original checkout's v4.2.2 workspace remains outside this worktree and unchanged.

### Task 5: Perform board-only confirmation when authorized and available

**Files:**
- No source edits expected.

**Interfaces:**
- Consumes: v4.4.0 M7 firmware and an authorized, connected NUCLEO-H755ZI-Q.
- Produces: Runtime confirmation that the shell command is present and starts/stops.

- [ ] **Step 1: Do not flash without authorization**

If no explicit authorization to flash has been given, stop after build and host-side checks.

- [ ] **Step 2: When authorized, flash through the existing helper**

Run the existing `scripts/flash_zephyr_hello_shell.sh` flow with board access and confirm the UART shell accepts:

```text
zyclictest start -i 400 -p -11
zyclictest stop
```

Expected: the command reports measurements without shell errors. Record whether the histogram has errors or overflows; a successful command invocation alone does not establish a valid worst-case bound.
