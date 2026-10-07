# Zephyr native_sim support design

## Goal

Run `apps/zephyr-hello-shell` both on the existing NUCLEO-H755ZI-Q and under
Zephyr's `native_sim`, while sharing the shell, HTTP dashboard/API, and MJPEG
application services. Each target is a separate build artifact from the same
source tree. `native_sim` is a host-based Zephyr target, not an emulator for
the STM32H755 or its peripherals.

## Architecture

Keep the Nucleo build and its runtime behavior unchanged. Select target-specific
implementations at build time, using separate build directories:

- The Nucleo selects the existing board I/O implementation and STM32-specific
  startup and USB host sources.
- `native_sim` selects a software implementation of the existing `board_io.h`
  contract. It stores LED state in software, retains blink behavior, and
  provides a shell command to set the simulated user button pressed or
  released.
- Put die-temperature access behind a target-selected interface. The Nucleo
  implementation reads the existing sensor. The simulator returns a clearly
  labeled synthetic temperature; it must not present the value as MCU telemetry.
- Exclude STM32 boot optimization and hardware USB host/controller sources from
  the simulator build. Report USB host as unsupported there and return an empty,
  disconnected mouse snapshot to the shared dashboard/API. Do not expose
  hardware-only USB shell commands in the simulator.

The source selection is by board at configure time, not runtime `#ifdef`s
scattered through shared services. The Nucleo's current build configuration,
addresses, ports, and peripherals remain target-specific and unchanged.

## Networking profiles

Provide two explicit native_sim configuration fragments, both separate from
the Nucleo configuration:

1. **TAP profile:** use Zephyr's native TAP Ethernet driver and normal Zephyr
   IPv4/TCP stack. Use Zephyr `net-tools` to create/configure the host TAP
   interface. This profile is for testing Zephyr networking as well as the
   application services.
2. **Localhost profile:** use Zephyr native_sim offloaded sockets so the host
   browser and command-line clients reach the service through loopback without
   TAP setup. This profile tests the application socket path and services, but
   not Zephyr's TCP/IP stack.

Make the dashboard port configurable by target. Keep the Nucleo at its current
port; use an unprivileged port (8080) for localhost mode and print the correct
simulator URL. Keep the camera stream on its existing port unless testing shows
a conflict. Use independent build directories for the two profiles.

## Runtime behavior and errors

- Shell, HTTP dashboard/API, logical LED control/blinking, simulated button
  state, synthetic temperature, and MJPEG streaming remain available on
  `native_sim`.
- Simulator startup identifies itself as simulation. USB-host absence is an
  explicit informational status, not a success-shaped hardware result.
- The mouse API stays structurally compatible with the dashboard and reports
  disconnected state.
- Synthetic temperature identifies its source as `simulated`; hardware mode
  retains its real die-sensor source.
- Simulator I/O operations preserve the existing API's error reporting. No
  unavailable physical device is silently treated as real.

## Build and validation

Build the Nucleo with the current Zephyr SDK flow and confirm the existing
flash/validation script remains usable. Build `native_sim/native/64` with the
host toolchain in separate TAP and localhost build directories. The 64-bit
target avoids requiring host multilib support.

Acceptance checks:

- Both Nucleo and native_sim builds complete from the same application source.
- Native_sim links no STM32 HAL/LL or USB-controller implementation.
- Native_sim starts the Zephyr shell, reports simulation mode, and supports
  LED status/control and blinking.
- Simulated button state can be changed and is reflected in `/api/v1/io`.
- The API labels the synthetic temperature and reports the mouse disconnected.
- The dashboard and camera stream respond to local clients in localhost mode.
- Dashboard/API respond over the configured TAP address in TAP mode.
- Existing Nucleo behavior, hardware temperature reporting, and USB host support
  remain intact.

TAP-mode execution requires the documented host TAP setup; localhost mode does
not. Neither mode validates STM32 register-level code, USB electrical behavior,
or physical-board timing.
