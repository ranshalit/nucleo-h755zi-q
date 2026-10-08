# Zephyr native_sim Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run the shared `zephyr-hello-shell` application on both the NUCLEO-H755ZI-Q and Zephyr `native_sim`, with working shell, dashboard/API, and MJPEG services in simulation.

**Architecture:** Select real-board or native_sim hardware backends at CMake configure time while keeping application services shared. Native_sim provides software LED/button state, synthetic temperature, and an explicit unsupported USB-host result. Two independent network fragments select Zephyr TAP networking or localhost offloaded sockets.

**Tech Stack:** Zephyr 4.2.2, west/CMake/Ninja, `native_sim/native/64` host toolchain, Zephyr networking, C, existing shell/API and C host-test scripts.

## Global Constraints

- Each target is a separate build artifact from the same source tree.
- `native_sim` is a host-based Zephyr target, not an emulator for the STM32H755 or its peripherals.
- The Nucleo's current build configuration, addresses, ports, and peripherals remain target-specific and unchanged.
- The native_sim TAP profile uses Zephyr's native TAP Ethernet driver and normal Zephyr IPv4/TCP stack.
- The native_sim localhost profile uses Zephyr native_sim offloaded sockets and does not validate Zephyr's TCP/IP stack.
- Use dashboard port 8080 for localhost mode; keep the Nucleo at its current port.
- Native-sim temperature must be labeled `simulated`; it must not be presented as MCU telemetry.
- Native-sim USB host is unsupported; its shared mouse snapshot reports disconnected.
- TAP mode requires the documented host TAP setup; localhost mode does not.
- Native_sim cannot validate STM32 register-level code, USB electrical behavior, or physical-board timing.

---

## File Map

| File | Responsibility |
|---|---|
| `apps/zephyr-hello-shell/CMakeLists.txt` | Select board-specific sources and config by `BOARD`; do not compile STM32-only files for native_sim. |
| `apps/zephyr-hello-shell/src/board_io_native_sim.c` | Implement the `board_io.h` contract with software LEDs and button state; register `sim button pressed|released`. |
| `apps/zephyr-hello-shell/src/usb_host/usb_host_native_sim.c` | Provide an explicit unsupported USB-host start result and an empty disconnected mouse snapshot without STM32 headers. |
| `apps/zephyr-hello-shell/src/board_temperature.h` | Define a target-neutral temperature value/source interface. |
| `apps/zephyr-hello-shell/src/board_temperature_nucleo.c` | Move existing STM32 die-temperature sensor sampling/cache from `http_server.c`. |
| `apps/zephyr-hello-shell/src/board_temperature_native_sim.c` | Return a stable synthetic temperature with source `simulated`. |
| `apps/zephyr-hello-shell/src/http_server.c` | Consume the target-neutral temperature interface; report valid network status when native offloaded sockets have no Zephyr interface. |
| `apps/zephyr-hello-shell/src/main.c` | Identify simulator startup and print the correct target endpoint; treat USB `-ENOTSUP` as an explicit informational state. |
| `apps/zephyr-hello-shell/Kconfig` | Add the configurable HTTP service port, defaulting to 80. |
| `apps/zephyr-hello-shell/prj.conf` | Keep common app/service settings; avoid making native_sim inherit board-only Ethernet assumptions. |
| `apps/zephyr-hello-shell/conf/native_sim_tap.conf` | Configure Zephyr TAP networking and the simulator address. |
| `apps/zephyr-hello-shell/conf/native_sim_localhost.conf` | Configure native_sim offloaded sockets and HTTP port 8080. |
| `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf` | Keep hardware-only ADC/sensor and USB selections on the Nucleo target; preserve current board behavior. |
| `README.md` | Document board and simulator builds, TAP setup, localhost use, simulated features, and hardware limitations. |

## Task 1: Split the Nucleo and native_sim source sets

**Files:**
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Create: `apps/zephyr-hello-shell/src/board_io_native_sim.c`
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_host_native_sim.c`
- Modify: `apps/zephyr-hello-shell/src/main.c`

**Interfaces:**
- Native board I/O implementation consumes `src/board_io.h` unchanged and implements every declared function: `board_io_init`, `board_blink_threads_start`, `board_blink_threads_suspend`, `board_blink_threads_resume`, `board_led_count`, `board_led_info_get`, `board_led_find`, `board_led_get`, `board_led_apply_action`, `board_button_label`, and `board_button_get`.
- `usb_host_native_sim.c` provides `int board_usb_host_start(void)` and `void usb_boot_mouse_snapshot_get(struct usb_boot_mouse_snapshot *snapshot)`. The start function returns `-ENOTSUP`; the snapshot is zeroed and disconnected.
- `main.c` continues the nonfatal path for USB errors, but logs `-ENOTSUP` as unsupported in simulation instead of a hardware initialization failure.

- [ ] **Step 1: Make native_sim source selection fail visibly on the current STM32-only source list**

Run from the Zephyr workspace:

```bash
cd vendor/zephyrproject/zephyr
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-native
```

Expected before changes: build fails compiling STM32-only files due to missing
`stm32h7xx.h` or `stm32h7xx_hal.h`. This confirms the current regression and
prevents treating native_sim as an MCU emulator.

- [ ] **Step 2: Add native software board I/O**

Implement the same LED metadata and blink periods as `board_io.c`. Store three
LED states and the button state in software protected by a Zephyr mutex. Make
`board_led_get` and `board_led_apply_action` return the same errors for invalid
IDs/actions as the board implementation. Keep the yellow/red blink threads and
periods. Add the shell command `sim button pressed|released`; reject any other
argument with a shell error and nonzero command result. Label the button
`B1 (simulated)`.

- [ ] **Step 3: Add the simulator USB boundary**

Implement `board_usb_host_start()` to return `-ENOTSUP`, and implement the mouse
snapshot getter using `memset(snapshot, 0, sizeof(*snapshot))` after checking
for a null pointer. For a null pointer, return without dereferencing it. Do not
include any STM32 HAL/CMSIS or STM32 USB-host headers in this file.

- [ ] **Step 4: Select target-specific CMake sources**

Keep `main.c`, `http_server.c`, web assets, MJPEG formatter/frames/stream, and
`usb_boot_mouse_report.c` in `target_sources(app ...)` for both supported
targets; the mouse report decoder is portable C. For
`nucleo_h755zi_q/stm32h755xx/m7`, retain `board_io.c`, `boot_optimizations.c`,
the current STM32 USB adapter, USB host shell/class/core sources, and existing
board `.conf`/overlay. Keep `src/usb_host` as an include directory for both
targets because the shared HTTP service uses `usb_boot_mouse.h`; keep
`third_party/stm32-mw-usb-host/Core/Inc` Nucleo-only. For
`native_sim/native/64`, select
`board_io_native_sim.c` and `usb_host_native_sim.c`, and omit
`boot_optimizations.c`, `stm32_hcd_adapter.c`, `usb_gpio_capture.c`,
`usb_host_shell.c`, `usb_boot_mouse_class.c`, and the STM32 USB middleware.
Keep the existing Nucleo config and devicetree overlay selected only for the
Nucleo target.

- [ ] **Step 5: Build both targets**

```bash
cd vendor/zephyrproject/zephyr
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-native
```

Expected: native_sim build completes without STM32 headers. Then build the
Nucleo with the existing Zephyr SDK flow from `README.md`; expected: existing
board target still configures and links with real USB/boot sources.

Verify the native build graph contains no board-only source paths:

```bash
if rg -n 'boot_optimizations\.c|stm32_hcd_adapter\.c|usb_gpio_capture\.c|third_party/stm32-mw-usb-host' \
  /tmp/stm32h7-zephyr-hello-shell-native/build.ninja; then
  echo "STM32-only source leaked into native_sim build" >&2
  exit 1
fi
```

- [ ] **Step 6: Commit**

```bash
git add apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/board_io_native_sim.c \
  apps/zephyr-hello-shell/src/usb_host/usb_host_native_sim.c \
  apps/zephyr-hello-shell/src/main.c
git commit -m "feat: add native_sim hardware backends" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 2: Separate real and synthetic temperature reporting

**Files:**
- Create: `apps/zephyr-hello-shell/src/board_temperature.h`
- Create: `apps/zephyr-hello-shell/src/board_temperature_nucleo.c`
- Create: `apps/zephyr-hello-shell/src/board_temperature_native_sim.c`
- Modify: `apps/zephyr-hello-shell/prj.conf`
- Modify: `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`
- Modify: `apps/zephyr-hello-shell/src/http_server.c`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`

**Interfaces:**

```c
struct board_temperature {
	int32_t milli_celsius;
	const char *source;
};

int board_temperature_get(struct board_temperature *temperature);
```

The Nucleo implementation uses the current die-temperature sensor node,
one-second refresh cache, and `sensor_value_to_milli()` behavior. Native_sim
returns `25000` milli-degrees Celsius with source `"simulated"`. Both return
`-EINVAL` for a null output and negative errno values on failure;
`http_server.c` returns the existing `{"available":false}` shape for an
unavailable reading and includes the implementation's source when available.

- [ ] **Step 1: Preserve temperature JSON formatting with source-driven output**

Refactor the temperature sample/read logic from `http_server.c` into the
Nucleo backend without changing the existing cache interval or error behavior.
Change `append_temperature_json()` to call `board_temperature_get()` and emit
the returned `source` string instead of hard-coding `"mcu_die"`. Add the
native_sim implementation returning `25000` and `"simulated"`.

- [ ] **Step 2: Select the temperature implementation**

Add `board_temperature_nucleo.c` to the Nucleo source list and
`board_temperature_native_sim.c` to the native_sim source list. Do not compile
the Nucleo implementation for native_sim; it contains sensor/devicetree
dependencies that are not part of the simulator. Move `CONFIG_ADC=y` and
`CONFIG_SENSOR=y` from `prj.conf` into the Nucleo board configuration so the
simulator does not enable unused hardware sensor subsystems.

- [ ] **Step 3: Verify target-specific API values**

Build both targets. Run native_sim and request `/api/v1/io`; verify
`temperature.available` is true, `temperature.source` is `"simulated"`, and
`temperature.celsius` is `25.0`. On the Nucleo, use the current validation flow
and verify successful sensor readings still report `"mcu_die"`; if the sensor
is unavailable, the existing unavailable JSON shape remains.

- [ ] **Step 4: Commit**

```bash
git add apps/zephyr-hello-shell/src/board_temperature.h \
  apps/zephyr-hello-shell/src/board_temperature_nucleo.c \
  apps/zephyr-hello-shell/src/board_temperature_native_sim.c \
  apps/zephyr-hello-shell/prj.conf \
  apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf \
  apps/zephyr-hello-shell/src/http_server.c \
  apps/zephyr-hello-shell/CMakeLists.txt
git commit -m "feat: simulate die temperature on native_sim" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 3: Add TAP and localhost profiles with accurate service status

**Files:**
- Modify: `apps/zephyr-hello-shell/Kconfig`
- Modify: `apps/zephyr-hello-shell/prj.conf`
- Create: `apps/zephyr-hello-shell/conf/native_sim_tap.conf`
- Create: `apps/zephyr-hello-shell/conf/native_sim_localhost.conf`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Modify: `apps/zephyr-hello-shell/src/http_server.c`
- Modify: `apps/zephyr-hello-shell/src/main.c`

**Interfaces:**
- Add `CONFIG_APP_HTTP_PORT` as an integer Kconfig option with default `80`.
- `board_http_server_port()` returns the configured port.
- Offloaded-socket mode advertises `127.0.0.1`; TAP mode advertises the configured TAP IPv4 address. Nucleo continues advertising its current configured address.

- [ ] **Step 1: Make the current HTTP port configurable without changing the board default**

Add `CONFIG_APP_HTTP_PORT` with default `80` to `Kconfig`. Replace the literal
port in `http_server.c` with `CONFIG_APP_HTTP_PORT`. Build the Nucleo before
adding simulator overrides and verify its port remains 80.

- [ ] **Step 2: Add isolated native_sim network fragments**

Create `conf/native_sim_tap.conf` selecting `CONFIG_ETH_NATIVE_TAP=y`, Zephyr
IPv4 and TCP, and `CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.55.2"` with
`CONFIG_NET_CONFIG_PEER_IPV4_ADDR="192.168.55.100"` for the documented host
TAP setup. Create
`conf/native_sim_localhost.conf` following Zephyr 4.2.2's
`samples/net/sockets/echo_server/overlay-nsos.conf`: disable TAP, enable
`CONFIG_NET_DRIVERS`, `CONFIG_NET_SOCKETS`, `CONFIG_NET_SOCKETS_OFFLOAD`, and
`CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`, and set `CONFIG_APP_HTTP_PORT=8080`.
Do not add either fragment to the Nucleo `CONF_FILE`.

- [ ] **Step 3: Make status and advertised URLs safe with offloaded sockets**

In `main.c`, identify `CONFIG_ARCH_POSIX` builds as simulation. Print the
localhost URL and configured HTTP port for
`CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`; print the TAP IPv4 URL for the TAP
profile. Keep the current Nucleo URL unchanged.

In `http_server.c`, guard network-interface state reads when
`net_if_get_default()` returns `NULL`; report unavailable/unknown interface
state instead of calling `net_if_is_*()` or `net_if_oper_state()` with a null
pointer. Do not assume `CONFIG_NET_CONFIG_PEER_IPV4_ADDR` exists in the
offloaded-socket profile. Keep status JSON field names and types stable, and
use `"127.0.0.1"` as the localhost-mode peer/advertised address.

- [ ] **Step 4: Build both networking profiles in distinct directories**

```bash
cd vendor/zephyrproject/zephyr
APP_DIR="$(realpath ../../../apps/zephyr-hello-shell)"
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-native-tap \
  -- -DEXTRA_CONF_FILE="${APP_DIR}/conf/native_sim_tap.conf"
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell-native-localhost \
  -- -DEXTRA_CONF_FILE="${APP_DIR}/conf/native_sim_localhost.conf"
```

Expected: both configure and link; the localhost build has HTTP port 8080 and
the TAP build keeps port 80. Then build the Nucleo in its existing build
directory and verify it still uses port 80 and its current static address.

- [ ] **Step 5: Run network/service smoke checks**

For localhost mode, run
`/tmp/stm32h7-zephyr-hello-shell-native-localhost/zephyr/zephyr.exe` and check
from a second terminal. Connect to the UART pseudoterminal reported at startup
and issue `leds on green`, `leds status`, and `sim button pressed`; then issue
`sim button released` and confirm the state changes back. Fetch `/api/v1/io`
after each button command and verify the JSON's `button.pressed` changes, LED
state is on, temperature source is `simulated`, and mouse is disconnected.
Also verify `/api/v1/status` reports the configured HTTP port and localhost
address.

```bash
curl --fail http://127.0.0.1:8080/
curl --fail http://127.0.0.1:8080/api/v1/io
curl --fail http://127.0.0.1:8080/api/v1/status
curl --fail http://127.0.0.1:8080/api/v1/mouse
curl --max-time 2 -sS -D - -o /dev/null http://127.0.0.1:8081/
```

Expected: dashboard and JSON endpoints return HTTP 200; mouse is disconnected;
camera stream response includes `multipart/x-mixed-replace`. The final curl may
exit 28 when its intentional two-second time limit expires after headers/data.
For TAP mode, set up the TAP interface using Zephyr `net-tools`, run the TAP
binary, configure the host TAP interface as `192.168.55.100/24`, and repeat
the dashboard/API checks against `192.168.55.2`.

- [ ] **Step 6: Commit**

```bash
git add apps/zephyr-hello-shell/Kconfig \
  apps/zephyr-hello-shell/prj.conf \
  apps/zephyr-hello-shell/conf/native_sim_tap.conf \
  apps/zephyr-hello-shell/conf/native_sim_localhost.conf \
  apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/http_server.c \
  apps/zephyr-hello-shell/src/main.c
git commit -m "feat: add native_sim network profiles" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 4: Document operation and verify board regression

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document native_sim build and run commands**

Add a section beside the existing Zephyr application instructions with the
exact separate build directories and both `west build` commands from Task 3.
Document that `native_sim/native/64` uses the host toolchain, that the binary is
`zephyr/zephyr.exe`, and that Ctrl+C stops it. Document connecting to the
printed UART pseudoterminal (for example, `screen /dev/pts/<n>`) to issue shell
commands.

- [ ] **Step 2: Document feature differences and network setup**

Explain that logical LEDs and button are software state, the shell command is
`sim button pressed|released`, temperature source is synthetic, USB host is
unsupported, and simulated mouse state is disconnected. Explain TAP needs
Zephyr `net-tools` and a configured host interface while localhost mode uses
`http://127.0.0.1:8080/` and does not exercise Zephyr's TCP/IP stack.

- [ ] **Step 3: Run repository tests and board regression build**

From the repository root:

```bash
scripts/run_mjpeg_multipart_tests.sh
scripts/run_usb_enumerator_tests.sh
```

Expected: both print `ALL TESTS PASSED`. Build both native_sim profiles and the
Nucleo. Finally run the existing board-only validation flow:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only
```

Expected: the Nucleo image still builds with STM32 HAL/LL and USB host enabled;
no flash operation is performed by `--build-only`.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document native_sim app workflows" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
