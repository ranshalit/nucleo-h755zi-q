# Zephyr QEMU AN500 Application Support

## Goal

Run `apps/zephyr-hello-shell` as Zephyr firmware under QEMU's Cortex-M7 MPS2
AN500 machine, reusing the application's existing simulation backends while
leaving NUCLEO-H755ZI-Q and `native_sim` behavior unchanged. This target models
an MPS2 AN500 system, not the STM32H755 or its peripherals.

## Architecture

Add `mps2/an500` as a separately selected application target in
`apps/zephyr-hello-shell/CMakeLists.txt`. It uses the existing software LED and
button implementation, synthetic temperature implementation, and unsupported
USB-host implementation. Generalize only the compile-time guard needed to
expose simulation controls and label USB as unsupported for both simulated
targets; preserve the existing `native_sim` network-specific behavior.

Add an AN500 configuration fragment and devicetree overlay. The overlay enables
the MPS2 LAN9220-compatible Ethernet node and maps it to QEMU AN500's LAN9118
address (`0xa0000000`). Keep the current static IPv4 configuration
(`192.168.55.2/24`) and the documented host TAP peer (`192.168.55.100/24`).
QEMU connects the emulated LAN9118 to the existing `zeth` TAP interface.

Build and run AN500 as a separate artifact using the host's existing
`arm-zephyr-eabi` cross-compile setup and `qemu-system-arm -machine mps2-an500`.
Do not patch vendored Zephyr board files or change the board's default FVP
runner selection; document direct QEMU invocation for this app.

## Runtime and errors

- The shell, HTTP dashboard/API, simulated LED and button state, synthetic
  temperature, and MJPEG stream run on AN500.
- Temperature remains explicitly labeled `simulated`.
- USB host remains explicitly unsupported; its mouse snapshot is disconnected.
- Network, HTTP, or stream startup failures retain the application's current
  explicit error reporting and nonfatal behavior where already defined.
- Nucleo hardware initialization and `native_sim` localhost/TAP behavior remain
  unchanged.

## Validation

1. Build `mps2/an500` and confirm the resulting image fits the board's Zephyr
   memory regions and links no STM32-specific implementation.
2. Run the image in QEMU with `zeth` and confirm shell boot plus the reported
   static address.
3. From the host TAP peer, verify dashboard and `/api/v1/status`,
   `/api/v1/io`, and `/api/v1/mouse` responses; confirm simulated temperature,
   disconnected mouse, and camera multipart response.
4. Run the existing native_sim and Nucleo build/test flows to detect
   regressions.

QEMU AN500 validation does not test STM32H755 register-level behavior, physical
USB, board LEDs/button, or hardware timing.
