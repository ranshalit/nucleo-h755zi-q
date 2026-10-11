# Zephyr QEMU AN500 Application Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run `apps/zephyr-hello-shell` as Zephyr firmware on QEMU's Cortex-M7 MPS2 AN500 machine, with simulated board I/O and the existing dashboard/API/MJPEG services reachable over TAP Ethernet.

**Architecture:** Add `mps2/an500` as an app-only target that selects the existing software LED/button, synthetic temperature, and unsupported USB backends. Add a target-specific network config and devicetree overlay mapping the MPS2 LAN9220-compatible Ethernet device to QEMU AN500's LAN9118 address. Keep the Nucleo and `native_sim` targets unchanged, and launch QEMU directly without changing vendored Zephyr runner configuration.

**Tech Stack:** Zephyr 4.2.2, west, CMake/Ninja, `arm-zephyr-eabi` cross-compiler, QEMU `mps2-an500`, Zephyr IPv4/TCP, QEMU TAP networking.

## Global Constraints

- The target models an MPS2 AN500 system, not the STM32H755 or its peripherals.
- Reuse the application's existing simulation backends while leaving NUCLEO-H755ZI-Q and `native_sim` behavior unchanged.
- Map the MPS2 LAN9220-compatible Ethernet node to QEMU AN500's LAN9118 address (`0xa0000000`).
- Keep the current static IPv4 configuration (`192.168.55.2/24`) and the documented host TAP peer (`192.168.55.100/24`).
- Do not patch vendored Zephyr board files or change the board's default FVP runner selection; document direct QEMU invocation for this app.
- Temperature remains explicitly labeled `simulated`; USB host remains explicitly unsupported and its mouse snapshot is disconnected.
- QEMU AN500 validation does not test STM32H755 register-level behavior, physical USB, board LEDs/button, or hardware timing.

---

## File Map

| File | Responsibility |
|---|---|
| `apps/zephyr-hello-shell/CMakeLists.txt` | Select existing simulation backends and the AN500-only config/overlay for `BOARD=mps2/an500`. |
| `apps/zephyr-hello-shell/src/main.c` | Expose simulation-only shell controls and USB-unsupported reporting on both simulated targets without changing native_sim network selection. |
| `apps/zephyr-hello-shell/src/web_assets.c` | Use generic board-I/O title text so the shared dashboard does not claim the emulated target is STM32H7 hardware. |
| `apps/zephyr-hello-shell/boards/mps2_an500.conf` | Enable the SMSC/LAN9220-compatible Ethernet driver for the AN500 app build. |
| `apps/zephyr-hello-shell/boards/mps2_an500.overlay` | Enable the MPS2 Ethernet node and move its MMIO base to QEMU AN500's `0xa0000000`. |
| `README.md` | Document the separate AN500 build, TAP setup, direct QEMU launch, simulated features, and limitations. |

## Task 1: Add the AN500 app target using simulated hardware backends

**Files:**
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Modify: `apps/zephyr-hello-shell/src/main.c`
- Modify: `apps/zephyr-hello-shell/src/web_assets.c`
- Create: `apps/zephyr-hello-shell/boards/mps2_an500.conf`
- Create: `apps/zephyr-hello-shell/boards/mps2_an500.overlay`

**Interfaces:**
- Consume the existing APIs in `src/board_io.h`, `src/board_temperature.h`, and `src/usb_host/usb_boot_mouse.h`.
- Add no new runtime APIs. `mps2/an500` and `native_sim/native/64` both compile `board_io_native_sim.c`, `board_temperature_native_sim.c`, and `usb_host_native_sim.c`.
- Define `APP_SIMULATED_HW` for both simulated targets. Use it to enable `sim button pressed|released` and to print the explicit USB-unsupported message. Keep network-specific `CONFIG_ARCH_POSIX` branches unchanged.

- [ ] **Step 1: Confirm the app currently rejects AN500**

Run from the Zephyr workspace:

```bash
cd vendor/zephyrproject/zephyr
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-
west build -p always -b mps2/an500 \
  ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-qemu-an500
```

Expected before the changes: app configuration fails with the existing
`Unsupported BOARD target: mps2/an500` CMake error.

- [ ] **Step 2: Add the AN500 board config and overlay**

Create `apps/zephyr-hello-shell/boards/mps2_an500.conf` with:

```conf
CONFIG_ETH_SMSC911X=y
CONFIG_ENTROPY_GENERATOR=y
CONFIG_TEST_RANDOM_GENERATOR=y
```

The MPS2 AN500 QEMU model has no entropy source. The test random generator is
needed by Zephyr's networking stack in this non-production simulation build;
do not treat it as cryptographically secure entropy.

Create `apps/zephyr-hello-shell/boards/mps2_an500.overlay` with:

```dts
&eth0 {
	reg = <0xa0000000 0x100000>;
	status = "okay";
};
```

The overlay is required because the shared MPS2 devicetree maps Ethernet for
other FPGA images, while QEMU's AN500 machine maps its emulated LAN9118 at
`0xa0000000`.

- [ ] **Step 3: Select simulated sources for AN500**

In `CMakeLists.txt`, add this board branch before the existing unsupported-board
`else`:

```cmake
elseif(BOARD STREQUAL "mps2/an500")
  set(extra_conf_file "${CMAKE_CURRENT_SOURCE_DIR}/boards/mps2_an500.conf")
  set(extra_dtc_overlay_file "${CMAKE_CURRENT_SOURCE_DIR}/boards/mps2_an500.overlay")
  set(board_sources
    src/board_io_native_sim.c
    src/board_temperature_native_sim.c
    src/usb_host/usb_host_native_sim.c
  )
  set(board_compile_definitions APP_SIMULATED_HW)
```

Change the `native_sim/native/64` branch's compile definition from
`APP_NATIVE_SIM` to `APP_SIMULATED_HW`; keep its source list, build selection,
and network config behavior unchanged.

- [ ] **Step 4: Generalize simulation-only shell/status guards**

In `src/main.c`, change both `#ifdef APP_NATIVE_SIM` directives to
`#ifdef APP_SIMULATED_HW`. This exposes the existing simulated button command
and prints `USB host unsupported in simulation` for AN500 as well as
native_sim. Do not change the `CONFIG_ARCH_POSIX` address-selection block.

- [ ] **Step 5: Remove STM32-only dashboard branding**

In `src/web_assets.c`, replace the document title and main heading with
`Board I/O dashboard` and `Board I/O`. The bundled page serves the Nucleo and
simulated profiles, so use generic wording rather than presenting AN500 as an
STM32H7 board.

- [ ] **Step 6: Build AN500 and verify the selected hardware boundary**

Repeat the Step 1 build. Expected: success, with `FLASH` and `RAM` usage within
the generated 4 MiB regions. Verify only simulated peripheral backends are in
the application source list:

```bash
grep -E 'board_io_native_sim.c|board_temperature_native_sim.c|usb_host_native_sim.c' \
  /tmp/stm32h7-zephyr-hello-shell-qemu-an500/build.ninja | head
if grep -E 'boot_optimizations.c|stm32_hcd_adapter.c|usb_gpio_capture.c|third_party/stm32-mw-usb-host' \
  /tmp/stm32h7-zephyr-hello-shell-qemu-an500/build.ninja; then
  echo "STM32-only source leaked into AN500 build" >&2
  exit 1
fi
```

Also run the focused portable C tests:

```bash
scripts/run_mjpeg_multipart_tests.sh
scripts/run_usb_enumerator_tests.sh
```

Expected: both print `ALL TESTS PASSED`.

- [ ] **Step 7: Commit the AN500 app profile**

```bash
git add apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/main.c \
  apps/zephyr-hello-shell/src/web_assets.c \
  apps/zephyr-hello-shell/boards/mps2_an500.conf \
  apps/zephyr-hello-shell/boards/mps2_an500.overlay
git commit -m "feat: add QEMU M7 application profile" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 2: Document and validate the QEMU run workflow

**Files:**
- Modify: `README.md`

**Interfaces:**
- Build target: `mps2/an500`.
- QEMU machine and NIC: `mps2-an500`, onboard LAN9118, TAP interface `zeth`.
- Guest/host addresses: `192.168.55.2/24` and `192.168.55.100/24`.
- HTTP dashboard/API: port 80. Camera multipart endpoint: port 8081.

- [ ] **Step 1: Add build and QEMU run instructions**

Add a `Run with QEMU MPS2 AN500 (Cortex-M7)` subsection alongside the
native_sim instructions in the Zephyr external app section. Include:

```bash
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-
cd vendor/zephyrproject/zephyr
west build -p always -b mps2/an500 \
  ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-qemu-an500
```

Then show launching the generated ELF directly:

```bash
qemu-system-arm -machine mps2-an500 -nographic \
  -kernel /tmp/stm32h7-zephyr-hello-shell-qemu-an500/zephyr/zephyr.elf \
  -nic tap,model=lan9118,ifname=zeth,script=no,downscript=no
```

Explain that direct launch is used because this app does not change Zephyr's
vendored AN500 FVP runner selection.

- [ ] **Step 2: Document TAP setup and simulated behavior**

Reuse the existing TAP setup instructions without duplicating host-interface
creation unnecessarily. State that `zeth` must be UP before QEMU starts and
that the app is an MPS2 AN500 M7 simulation, not an H755 emulation. Document
the logical LEDs, `sim button pressed|released`, synthetic temperature,
unsupported USB, and disconnected mouse behavior.

- [ ] **Step 3: Run the app under QEMU and verify services**

With `zeth` configured and UP, launch the ELF as in Step 1. Confirm the console
prints `Hello World! mps2/an500`, the dashboard URL
`http://192.168.55.2:80/`, and USB-host unsupported status.

At the QEMU shell, exercise the simulated controls:

```text
uart:~$ leds on green
green -> on
uart:~$ sim button pressed
B1 (simulated) pressed
uart:~$ sim button released
B1 (simulated) released
```

From the host, run:

```bash
ping -c 1 192.168.55.2
curl --fail http://192.168.55.2/
curl --fail http://192.168.55.2/api/v1/status
curl --fail http://192.168.55.2/api/v1/io
curl --fail http://192.168.55.2/api/v1/mouse
curl --max-time 2 -sS -D - -o /dev/null http://192.168.55.2:8081/
```

Expected: dashboard and API endpoints return HTTP 200; `/api/v1/io` reports
green LED on, button state matching the last shell command, and temperature
source `simulated`; `/api/v1/mouse` reports disconnected; the camera response
headers contain `multipart/x-mixed-replace`. The final curl may exit 28 after
the intentional timeout, after printing response headers.

- [ ] **Step 4: Verify existing profiles and tests remain unchanged**

```bash
scripts/run_zephyr_hello_shell_native.sh --profile localhost --build-only
scripts/run_zephyr_hello_shell_native.sh --profile tap --build-only
scripts/flash_zephyr_hello_shell.sh --build-only
```

Expected: both native_sim profiles and the Nucleo image build successfully;
the Nucleo command does not flash hardware. Re-run
`scripts/run_mjpeg_multipart_tests.sh` and
`scripts/run_usb_enumerator_tests.sh`; both must print `ALL TESTS PASSED`.

- [ ] **Step 5: Commit the workflow documentation**

```bash
git add README.md
git commit -m "docs: document QEMU M7 app workflow" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
