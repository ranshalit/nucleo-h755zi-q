# General

This repository is a playground for the STM32H7 Nucleo-144 board,
specifically the `NUCLEO-H755ZI-Q`.

It is used for:

- building Zephyr
- programming and validating on real hardware
- changing pinmux
- boot-time optimization
- driver customization
- adding devices

## Configuration

The board is connected to the host over USB.

The ST-LINK VCP serial console is exposed on `/dev/ttyACM0` or a stable
`/dev/serial/by-id/...` path.

## Installation

Clone the repository and initialize the vendored Zephyr workspace submodules:

```bash
git clone <repo-url>
cd stm32h7
git submodule update --init --recursive
```

Install the host-side tools used by the verified Zephyr flow:

```bash
python3.12 -m venv "$HOME/.venvs/zephyr-4.4"
source "$HOME/.venvs/zephyr-4.4/bin/activate"
python -m pip install --disable-pip-version-check west ninja pyelftools
cd vendor/zephyrproject/zephyr
python -m pip install --disable-pip-version-check -r scripts/requirements.txt
```

Zephyr v4.4.0 requires Python 3.12 or newer.
Install the Zephyr SDK ARM toolchain and use it instead of unrelated local
`arm-none-eabi` toolchains when validating on hardware.

## Zephyr external app

The repo-owned Zephyr application is:

`apps/zephyr-hello-shell`

It keeps the application and shell configuration in this repository instead of
patching the upstream Zephyr source tree under `vendor/zephyrproject/zephyr`.

The M7 build combines upstream board defaults with app-specific overrides.
Paths below are relative to the repository root:

| Layer | File |
| --- | --- |
| Board devicetree | `vendor/zephyrproject/zephyr/boards/st/nucleo_h755zi_q/nucleo_h755zi_q_stm32h755xx_m7.dts` |
| Shared board devicetree definitions | `vendor/zephyrproject/zephyr/boards/st/nucleo_h755zi_q/nucleo_h755zi_q.dtsi` |
| Board default Kconfig settings | `vendor/zephyrproject/zephyr/boards/st/nucleo_h755zi_q/nucleo_h755zi_q_stm32h755xx_m7_defconfig` |
| App devicetree overrides | `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.overlay` |
| App board-specific configuration | `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf` |
| Common app configuration | `apps/zephyr-hello-shell/prj.conf` |

The devicetree overlay contains these app-specific hardware settings:

| Override | Purpose |
| --- | --- |
| PD10 USB VBUS control | Drive PD10 high at startup to disable the active-low USB power switch until the app controls it. |
| ADC3 and die-temperature sensor | Enable the internal temperature sensor and configure the ADC clock source and prescaler. |
| Speech-model flash region | Declare 128 KiB at `0x08120000` in flash bank 2 for model data; M4 boot is disabled separately in the board-specific `.conf`. |
| SRAM3 memory attributes | Mark Ethernet DMA descriptor/buffer memory non-cacheable so CPU accesses bypass D-cache while other memory can remain cacheable. |

After a VS Code build, inspect the merged devicetree in
`build/zephyr-hello-shell/zephyr/zephyr.dts` and the resolved Kconfig settings in
`build/zephyr-hello-shell/zephyr/.config`. The flash script's default build
directory puts these files under `/tmp/stm32h7-zephyr-hello-shell/zephyr/`
instead. Edit the app's `.overlay` and `.conf` files, not these generated files.

CPU runtime statistics are enabled for this app. On the UART shell, run
`cpu usage` for CPU busy/idle percentages over 1 second, or
`cpu usage 5000` for a 5-second interval (1–60000 ms). This measures the
M7 Zephyr core, not the separate M4 core. `kernel thread list` also shows
each thread's cumulative execution cycles and CPU percentage since boot;
its `stack size ... (% ...)` lines instead report peak stack use, not CPU.

The M7-only `zyclictest` shell command is enabled with Zephyr v4.4.0. For
example, start a 400-microsecond cyclic test at priority -11 and stop it with:

```text
uart:~$ zyclictest start -i 400 -p -11
uart:~$ zyclictest stop
```

The M7 configuration is tickless with `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000`;
tickless mode suppresses periodic ticks but the configured tick rate still
sets the kernel's timekeeping resolution. The command has been build-verified,
but its runtime measurements have not yet been validated on hardware.
See [latency testing](#standalone-latency-testing-with-zyclictest) for the
standalone benchmark and measurement under webserver load.

For a one-command CTF capture, connect ST-LINK and run from the repository
root (no separate OpenOCD or GDB terminal needed):

```bash
scripts/capture_zephyr_trace.sh
```

This snapshots the existing M7 RAM recording without resetting or flashing.
It starts its own OpenOCD, halts M7 briefly to copy the data, resumes/detaches,
and stops its OpenOCD. Stop other debug sessions first. Output includes
`channel0_0` and CTF `metadata`, saved to a new timestamped directory under
`~/zephyr-traces/`, outside the disposable build tree. Diagnostic logs live in
its hidden `.capture/` subdirectory, separate from CTF stream files.

To explicitly reset the board and wait 20 seconds before capturing a fresh
boot recording:

```bash
scripts/capture_zephyr_trace.sh --reset --seconds 20
```

`--seconds` requires `--reset` and defaults to 20 when reset is requested.
It controls the wait before dumping, **not** recording duration: the finite
16 KiB buffer stops recording when full. Reset clears any previous recording.
If snapshot attachment fails immediately after flashing, use `--reset` only
when clearing the recording and restarting the board is acceptable.

The default ELF is `build/zephyr-hello-shell/zephyr/zephyr.elf`; it must match
the flashed image. Override it for the flash script's default build:

```bash
scripts/capture_zephyr_trace.sh \
  --elf /tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.elf \
  --output "$HOME/zephyr-traces/my-capture"
```

`--output` must name a new directory. Use the Zephyr checkout/metadata matching
the flashed firmware. Debugger helper environment overrides apply; see
`scripts/capture_zephyr_trace.sh --help`. Python 3 is used for host-side checks;
GDB Python support is not required. On failure, inspect the printed log
directory; a recovery debugger session attempts to resume/detach if extraction
fails.

Check a recording with `babeltrace2 <capture-directory>`. In Trace Compass,
use **File > Import > Tracing > Traces**, select the capture directory, then
right-click the imported trace and choose **Open**. Select generic **CTF**
if a trace type is requested. For the task timeline, follow the XML-analysis
instructions below. Old imports pointing into a deleted build directory
will not work; import the persistent capture instead.

The M7 board build also records Zephyr scheduler/interrupt events as CTF in a
16 KiB RAM buffer. This is a finite snapshot: recording stops when the buffer
fills, and a reset clears it. To view it with the open-source Eclipse Trace
Compass, flash the board, let it run briefly, then attach with
`scripts/debug_zephyr_hello_shell.sh` and connect GDB using the ELF from that
same build (see [Shell debugging](#shell-debugging-st-link--openocd--gdb)).
With M7 halted, in GDB run:

```gdb
dump binary memory channel0_0 ram_tracing (ram_tracing + 'tracing_backend_ram.c'::pos)
```

Put `channel0_0` beside a copy of
`vendor/zephyrproject/zephyr/subsys/tracing/ctf/tsdl/metadata` in a directory,
then import that directory as a CTF trace in Trace Compass (or inspect it
with `babeltrace2 <directory>`). Dump only through the backend's `pos` cursor:
the unused tail of the RAM array is not a valid CTF event. Only M7 Zephyr
events are recorded, not M4 activity. The ST-LINK virtual COM port remains
available for the shell.

Importing adds the trace to Project Explorer but does not open it: right-click
the imported `trace` under **Traces** and select **Open** to display its events,
histogram, and statistics. For a per-task CPU timeline, import
`scripts/zephyr-ctf-scheduling.xml` through **Window > Preferences > Tracing >
XML Analyses > Import**, enable it, then reopen the trace. Expand **Views**
under the trace and open **Zephyr task CPU timeline**. Green means running,
gray means switched out; the chart only covers captured switch events, not
task readiness or scheduling after the RAM buffer fills. If the initial view
is empty, click a busy region around 13 seconds on the **Histogram** tab to
move the synchronized 100 ms timeline window to recorded task switches. Rows
are keyed by thread name, so threads with identical names share a row; ISR
execution is not subtracted from a task's green interval.

For example, if GDB saved `channel0_0` in the repository root:

```bash
mkdir -p build/zephyr-hello-shell/trace
mv channel0_0 build/zephyr-hello-shell/trace/
cp vendor/zephyrproject/zephyr/subsys/tracing/ctf/tsdl/metadata \
  build/zephyr-hello-shell/trace/
babeltrace2 build/zephyr-hello-shell/trace
```

At runtime, dedicated Zephyr threads toggle the yellow LED (LD2) and red LED
(LD3), alongside the streaming, speech, USB, and system workers described below.
The green LED (LD1) remains available through the serial shell commands.
The same application also runs
a plain HTTP server on the board Ethernet interface and exposes built-in board
I/O through both JSON API endpoints and a browser dashboard:

- LEDs `LD1`, `LD2`, `LD3`
- user button `B1`
- board and network status

### Runtime threads

Run `kernel thread list` on the UART shell to inspect threads. The M7 hardware
build with speech AI and the USB event worker enabled showed these 16 threads;
optional features, diagnostic commands, and simulation builds can change the list.

#### Application threads

| Thread | Priority in snapshot | Purpose |
| --- | --- | --- |
| `audio_stream` | 7 | Streams USB microphone PCM audio over HTTP on port 8082 to one client. Waits for socket activity with a short poll timeout. |
| `camera_stream` | 7 | Serves an MJPEG loop of prerecorded JPEG frames on port 8081, not live camera capture. Sleeps 125 ms between frames (about 8 fps). |
| `speech_ai` | 11 | Converts 48 kHz stereo microphone audio to 16 kHz mono, extracts features, runs English keyword recognition, and filters predictions into recognized events. Priority 11 lets the USB event worker preempt inference; polling builds use speech priority 8. |
| `stm32_usbh` | 10 | Runs the ST USB host state machine for enumeration, transfers, mouse reports, and microphone capture. The default event-worker mode waits for notifications; diagnostic polling mode uses the same thread. |
| `board_io_error` | 8 | Waits for LED error messages and prints failures from privileged context, keeping logging outside the small userspace LED threads. A pending state does not indicate an error. |
| `red_led_blink` | 7 | Toggles red LED LD3, then sleeps for its configured interval. Runs in userspace with restricted memory and device access. |
| `yellow_led_blink` | 7 | Toggles yellow LED LD2 with independent timing and a separate memory domain. Shares the red thread's entry function, with different arguments. |

#### Zephyr infrastructure threads

| Thread | Priority in snapshot | Purpose |
| --- | --- | --- |
| `http_server_tid` | -1 | Serves the port-80 dashboard, static web assets, and APIs for board I/O, USB devices, and speech results/settings. Separate from the audio/video streaming threads. |
| `stm_eth` | -14 | STM32 Ethernet driver worker for deferred receive processing and forwarding received frames toward the network stack. |
| `tx_q[0]` | -1 | Processes queued outgoing packets for network traffic class 0 toward the Ethernet driver. |
| `rx_q[0]` | -1 | Processes incoming packets for network traffic class 0 through the protocol stack. |
| `net_mgmt` | -1 | Dispatches network-management events and callbacks, such as interface, link, and address changes; not normal packet-data processing. |
| `tcp_work` | -14 | Dedicated TCP workqueue for deferred processing, retransmissions, delayed ACKs, connection timers, and cleanup. |
| `shell_uart` | 9 | Runs UART shell commands and prints their output. The `*` marker identifies the thread executing the listing. |
| `sysworkq` | -1 | Shared system workqueue for deferred work submitted by drivers, kernel components, and application code. |
| `idle` | 15 | Runs when no other thread is ready and enables CPU idle/power-saving behavior. Its runtime is idle time, not useful application work. |

The `main` thread is absent after startup returns: the created workers keep
running. Workqueue threads such as `net_mgmt`, `tcp_work`, and `sysworkq` can
share an entry address because they use the same generic workqueue function.

#### Interpreting `kernel thread list`

| Field | Meaning |
| --- | --- |
| Leading address / `entry` | Address of the thread object / its entry function. |
| `priority` | Smaller number means higher priority: 7 outranks 11. Negative priorities are cooperative; ordinary threads cannot preempt them, so they must block or yield. Interrupts can still run. |
| `state: pending` | Waiting for an object, event, or I/O; normally not consuming CPU while blocked. |
| `state: sleeping` | Explicit timed sleep, such as the delay between LED toggles or video frames. |
| `state: queued` | Ready/runnable, including the current thread. The listing is collected over time, not an atomic snapshot. A blank idle state alone does not indicate a fault. |
| `Total execution cycles` / CPU percentage | Cumulative runtime accounting, not instantaneous utilization. Percentages round down, so 0% does not mean never executed. This Zephyr shell command prints cycle totals truncated to 32 bits, which can wrap, while calculating percentages from wider counters. |
| `stack size`, `unused`, `usage` | Stack capacity and observed high-water usage, not current consumption. Unused space in this snapshot does not guarantee worst-case safety. |
| `timeout` | Raw internal timeout-queue ticks, not milliseconds or directly comparable remaining delays. The large negative value is an internal sentinel, not an enormous sleep or an error; zero does not imply runnable. |
| `options: 0x4` | `K_USER`: userspace thread, as used by the LED workers. |
| `options: 0x1` | `K_ESSENTIAL`: aborting this thread is a fatal system error. |
| `options: 0x0` | Neither of those flags is set. |
| `Scheduler: ... since last call` | Timer ticks elapsed since the last clock announcement, not a context-switch count. |

In the supplied 2026-10-25 snapshot, speech AI accounted for 54% of runtime,
USB host 4%, and idle 35%. These are cumulative observations, not fresh interval
measurements or guaranteed workload costs. Use `cpu usage 5000` for a five-second
busy/idle measurement. The highest shown stack percentage was `sysworkq` at
688/1024 bytes (67%), leaving 336 bytes untouched; no shown stack was exhausted.

### Run with Zephyr native_sim

The same application can run on Zephyr's `native_sim/native/64` host target.
This uses the host toolchain; it simulates application behavior, not the
STM32H755 or its peripherals. The TAP and localhost profiles use separate
build directories so their network configurations stay independent.

Build and run from any directory using the repository's script:

```bash
scripts/run_zephyr_hello_shell_native.sh
```

This builds the localhost profile with `west build`, then runs its host
executable in the foreground. Open `http://127.0.0.1:8080/` in a browser.
Press Ctrl+C to stop it. Other options:

```bash
scripts/run_zephyr_hello_shell_native.sh --build-only
scripts/run_zephyr_hello_shell_native.sh --run-only
scripts/run_zephyr_hello_shell_native.sh --profile tap
scripts/run_zephyr_hello_shell_native.sh --profile tap --build-only
scripts/run_zephyr_hello_shell_native.sh --build-dir /tmp/my-native-build
```

The TAP profile requires a host TAP interface configured separately as
`192.168.55.100/24`; the script does not modify host networking. `--run-only`
requires a successful earlier build of the selected profile. Set `WEST_BIN`
if `west` is not on `PATH`. The script derives the application and Zephyr
paths from its own location, so it works from a worktree or after merging
into the main checkout.

To build manually instead, run these commands from the repository root:

```bash
export PATH="$HOME/.local/bin:$PATH"
REPO_ROOT="$PWD"
APP_DIR="${REPO_ROOT}/apps/zephyr-hello-shell"
ZEPHYR_DIR="${REPO_ROOT}/vendor/zephyrproject/zephyr"
cd "$ZEPHYR_DIR"

ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 "$APP_DIR" \
  -d "${REPO_ROOT}/build/zephyr-hello-shell-native-tap" \
  -- -DEXTRA_CONF_FILE="${APP_DIR}/conf/native_sim_tap.conf"

ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 "$APP_DIR" \
  -d "${REPO_ROOT}/build/zephyr-hello-shell-native-localhost" \
  -- -DEXTRA_CONF_FILE="${APP_DIR}/conf/native_sim_localhost.conf"
```

Each executable is at
`build/zephyr-hello-shell-native-{tap,localhost}/zephyr/zephyr.exe`, relative
to the repository root. Run the profile you want from the repository root:

```bash
build/zephyr-hello-shell-native-localhost/zephyr/zephyr.exe
```

The simulator prints the UART pseudoterminal path at startup. In another
terminal, connect to that path (for example,
`screen /dev/pts/<n>`) to use the Zephyr shell. Press Ctrl+C in the terminal
running `zephyr.exe` to stop the simulator.

#### Localhost networking

The localhost profile serves the dashboard and API at
`http://127.0.0.1:8080/`, with the API available under `/api/v1/`. It uses
native_sim offloaded sockets and does **not** exercise Zephyr's TCP/IP stack.
No TAP interface or host network reconfiguration is needed.

#### TAP networking

The TAP profile uses Zephyr's native TAP Ethernet driver and Zephyr's normal
IPv4/TCP stack. It uses `192.168.55.2/24` for the simulated board and expects
the host TAP interface at `192.168.55.100/24`. No Ethernet cable or physical
board is needed. On Linux, check whether `zeth` already exists:

```bash
ip -brief addr show dev zeth
```

If it does not exist, create and configure it (requires `sudo`):

```bash
sudo ip tuntap add dev zeth mode tap user "$(id -un)"
sudo ip addr add 192.168.55.100/24 dev zeth
sudo ip link set zeth up
```

If `zeth` already exists, do not recreate it; add the address or bring it up
only if needed. Start the simulator after the interface is configured, or
restart it if it was already running:

```bash
scripts/run_zephyr_hello_shell_native.sh --profile tap
```

In another terminal, check `ping 192.168.55.2` and open
`http://192.168.55.2/`. The script does not configure or remove the host TAP
interface. When finished, stop the simulator with Ctrl+C; if you created
`zeth` solely for this run, remove it with
`sudo ip tuntap del dev zeth mode tap`. Do not remove a pre-existing interface.

> **Switching back to the physical board:** the simulator and the Nucleo board
> use the same `192.168.55.0/24` subnet. A persistent `zeth` that still holds
> `192.168.55.100` captures the route even when it is down (`NO-CARRIER`), so
> pings to the real board time out. Before using hardware, release the address
> with `sudo ip addr del 192.168.55.100/24 dev zeth` (see
> [Ethernet validation](#ethernet-validation)).

#### Simulation differences

- LEDs and button are logical software state, not physical GPIO.
- Change the simulated button from the shell with `sim button pressed` or
  `sim button released`.
- Temperature readings are synthetic and labeled with source `simulated`;
  they are not MCU die telemetry.
- USB host hardware is unsupported in native_sim. The simulated mouse state
  remains disconnected.
- native_sim does not validate STM32 register-level behavior, USB electrical
  behavior, or physical-board timing. Use the Nucleo build and hardware for
  those checks.

### Run with QEMU MPS2 AN500 (Cortex-M7)

The app also runs as Zephyr firmware on QEMU's MPS2 AN500 Cortex-M7 machine.
This emulates an MPS2 system, not the STM32H755 or its peripherals. The app
uses the same logical LED/button, synthetic temperature, and unsupported USB
backends as native_sim, while Ethernet uses QEMU's emulated LAN9118. The
dashboard and API are served at `http://192.168.55.2/`, and the MJPEG stream at
`http://192.168.55.2:8081/`.

From the repository root, build and run with the helper:

```bash
scripts/run_zephyr_hello_shell_qemu.sh
```

By default it builds into `build/zephyr-hello-shell-qemu-an500` and runs QEMU
with user-mode networking, forwarding the dashboard/API to
`http://127.0.0.1:8080/` and the camera stream to
`http://127.0.0.1:8081/`. Press Ctrl+C to stop. Other useful modes:

```bash
scripts/run_zephyr_hello_shell_qemu.sh --build-only
scripts/run_zephyr_hello_shell_qemu.sh --run-only
scripts/run_zephyr_hello_shell_qemu.sh --network tap
scripts/run_zephyr_hello_shell_qemu.sh --debug
```

The `tap` mode uses Zephyr's regular TCP/IP path; it expects the host interface
`zeth` configured at `192.168.55.100/24` and up before starting. See the TAP
networking instructions above. At the QEMU shell, use `leds on green` and
`sim button pressed|released` to exercise the simulated controls. The mouse
remains disconnected, temperature is synthetic, and
`CONFIG_TEST_RANDOM_GENERATOR` supplies non-cryptographic test entropy only;
this profile is not suitable for security-sensitive use.

No changes to Zephyr's vendored board runner are needed; the helper launches
QEMU directly because the upstream AN500 board runner selects FVP.

### Build

```bash
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-

cd vendor/zephyrproject/zephyr
west build -p always \
  -b nucleo_h755zi_q/stm32h755xx/m7 \
  ../../../apps/zephyr-hello-shell \
  -d /tmp/stm32h7-zephyr-hello-shell
```

### Programming

```bash
export PATH="$HOME/.local/bin:$PATH:/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin:$PATH"

cd vendor/zephyrproject/zephyr
west flash \
  -d /tmp/stm32h7-zephyr-hello-shell \
  --runner stm32cubeprogrammer
```

### Helper script

The same Zephyr flow is wrapped in:

```bash
scripts/flash_zephyr_hello_shell.sh
```

Useful options:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only
scripts/flash_zephyr_hello_shell.sh --flash-only
scripts/flash_zephyr_hello_shell.sh --validate-only
scripts/flash_zephyr_hello_shell.sh --port /dev/ttyACM0
```

If M7 console output is not appearing, confirm the option bytes still boot CM7:

```bash
PROG=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI

"$PROG" -c port=SWD mode=UR reset=HWrst freq=8000 \
  -ob BCM4=0 BCM7=1 BOOT_CM7_ADD0=0x800 BOOT_CM4_ADD0=0x810
```

### Reboot (hardware reset over ST-LINK)

Reset the board without unplugging anything; `/dev/ttyACM0` stays open, so a
running serial monitor keeps logging the new boot:

```bash
/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI -c port=SWD mode=UR -hardRst
```

This pulses NRST (same as pressing B2 RESET, requires JP3 `T_NRST` fitted).
It is a reset, not a power cycle: supplies stay on, but the MCU and the
LAN8742 Ethernet PHY (whose reset is tied to NRST) both restart.

When powering from external 5V (JP2 on 5-6), UM2408 §7.4.6 says to apply 5V
before connecting the ST-LINK USB. If 5V is cycled with the ST-LINK still
connected, the board may glitch during power-up (a doubled
`*** Booting Zephyr OS build ***` banner). The firmware soft-resets the PHY at
init so Ethernet still comes up; if anything else looks wrong, run the reset
command above.

### Serial validation

The v4.4.0 image was built and flashed on 2026-10-25; runtime validation was
skipped by request. This is expected output, not a v4.4.0 hardware capture.
With the programmed image running, `/dev/ttyACM0` at
`115200 8N1` should show:

```text
*** Booting Zephyr OS build v4.4.0 ***
Hello World! nucleo_h755zi_q/stm32h755xx/m7
Blink threads started for yellow (400 ms) and red (700 ms)
HTTP server listening on http://192.168.55.2:80/

uart:~$
```

At the shell prompt, validate:

```text
uart:~$ help
Available commands:
...
uart:~$ kernel version
Zephyr version 4.4.0
uart:~$ leds on green
green -> on
uart:~$ leds off all
green -> off
yellow -> off
red -> off
```

Yellow and red should continue blinking independently after boot, and
`scripts/flash_zephyr_hello_shell.sh` now validates that behavior by sampling
their shell-reported states over time on the connected board.

### Debugging

#### QEMU debugging (GDB)

Build with symbols, then start QEMU halted at reset with the local GDB server:

```bash
scripts/run_zephyr_hello_shell_qemu.sh --debug
```

In a second terminal, start the Zephyr SDK GDB with the ELF from the same
build, then connect and set a breakpoint:

```bash
~/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb \
  build/zephyr-hello-shell-qemu-an500/zephyr/zephyr.elf
```

At the GDB prompt:

```gdb
target remote localhost:3333
break main
continue
```

QEMU starts halted before application execution, so the breakpoint can catch
`main` on its first run. Use `next`, `step`, `bt`, and `continue` to debug;
Ctrl+C interrupts execution. Stop QEMU with Ctrl+C in its terminal. This is
debugging of the emulated MPS2 AN500 Cortex-M7, not the STM32H755.

#### Shell debugging (ST-LINK / OpenOCD / GDB)

Use the board's built-in ST-LINK/V3 to debug the Zephyr Cortex-M7 app. No
SEGGER probe is needed. Build and flash the app as above first; use the ELF
from the **same build** as the running image so symbols match. On this host,
STM32CubeIDE supplies OpenOCD and its STM32H7 target scripts. The OpenOCD
installation does not include the `stm32h745i-disco.cfg` board file referenced
by Zephyr's board config, so `west attach --runner openocd` does not work with
that installation. The repo-owned config at
`scripts/stm32h755zi_cm7.openocd.cfg` instead uses the installed ST-LINK and
STM32H7 target scripts without modifying vendored Zephyr.

In terminal 1, from the repository root, start the GDB server:

```bash
scripts/debug_zephyr_hello_shell.sh
```

If STM32CubeIDE is installed elsewhere, set `STM32_OPENOCD` to its `openocd`
binary and `STM32_OPENOCD_SCRIPTS` to its `st_scripts` directory. The helper
reports missing tools or scripts. Stop any other OpenOCD/GDB session first;
only one process can use the ST-LINK probe at a time. If you start it within a
few seconds of flashing and OpenOCD logs `Examination failed`, wait a few
seconds and restart it, or use `--connect-under-reset` (resets the MCU).

In terminal 2, connect GDB to the already running image (no reflash), using
the ELF from the same build. For the VS Code workspace build described below:

```bash
scripts/gdb_zephyr_hello_shell.sh \
  build/zephyr-hello-shell/zephyr/zephyr.elf
```

Loading the ELF only loads symbols into GDB; it does **not** connect to the
board. At the `(gdb)` prompt, enter these commands in order. Wait for
`Remote debugging using localhost:3333` and a source location after the first
command before using `monitor` (otherwise GDB reports `"monitor" command not
supported by this target`):

```gdb
target extended-remote localhost:3333
break main
monitor reset halt
continue
```

Attaching halts M7. `main` has already run if you attach to a running app, so
reset before continuing to hit its breakpoint; a reset also restarts the app
and network connections. When the breakpoint hits, use `bt` to inspect the
call stack. Use `Ctrl-C` in GDB to interrupt a running target,
`next` or `step` to inspect execution, `continue` to resume, and `detach` to
leave it running. Stop the OpenOCD server with `Ctrl-C` in terminal 1. This
app is built with speed optimizations, so some variables may be reported as
`<optimized out>` and source stepping may jump. Debugging M4 is not covered by
this M7 configuration.

#### VS Code debugging (build, flash, and debug)

Open the **repository root** as the VS Code workspace. Install the Microsoft
**C/C++** extension and connect the board's ST-LINK USB. The workspace tasks
use the Zephyr SDK, `west`, and STM32CubeProgrammer; the debugger uses
STM32CubeIDE's OpenOCD and the SDK's GDB. Stop any other GDB/OpenOCD or
STM32CubeProgrammer session using the ST-LINK before starting.

1. **Build only:** select **Terminal > Run Build Task** (`Ctrl+Shift+B`), which
   runs **Zephyr M7: build**. This needs no board. It creates
   `build/zephyr-hello-shell/zephyr/zephyr.elf` and `.hex` under the workspace,
   not `/tmp`; `build/` is ignored by Git.
2. **Flash only:** after a successful build, select **Terminal > Run Task >
   Zephyr M7: flash**. This programs the workspace build through the ST-LINK,
   without running serial/network validation. The board must be connected.
3. **Build, flash, and debug in one step:** select **Run and Debug >
   Zephyr M7 (ST-LINK, build and debug)** and press F5. Its pre-launch task
   builds and flashes in that order, then VS Code starts OpenOCD with
   `--connect-under-reset`, connects GDB to port 3333 using the newly built
   ELF, and pauses M7 at the reset vector. You do **not** need to start
   OpenOCD separately for this configuration. A build or flash failure stops
   the launch; inspect the task's terminal output. Connecting under reset is
   required here: immediately after STM32CubeProgrammer starts the app, a
   plain attach fails with `[stm32h7x.cm7] Examination failed` and VS Code
   reports `Remote connection closed`. It needs JP3 `T_NRST` fitted.
4. **Use breakpoints:** set one in
   `apps/zephyr-hello-shell/src/main.c`, then press F5 to continue; the
   session starts at reset, so `main` breakpoints hit without an extra reset.
   To restart, enter `-exec monitor reset halt` in VS Code's **Debug
   Console**. Reset restarts the app and network connections. Inspect
   Variables, Watch, Call Stack, and Breakpoints; use F10/F11 to step. Stop
   the debug session when finished.

The build task finds `west` under `~/.local/bin` and the SDK under
`~/.local/zephyr-sdk-*`. The debugger launchers find STM32CubeIDE's OpenOCD
scripts and SDK GDB. For nonstandard installs, set `ZEPHYR_SDK_ROOT`,
`STM32_PROGRAMMER_CLI`, `STM32_OPENOCD`, `STM32_OPENOCD_SCRIPTS`, or
`ZEPHYR_GDB` in the VS Code environment. If flashing reports
`DEV_USB_COMM_ERR` or OpenOCD reports `read version failed`, the ST-LINK is
not communicating; the ELF can still be built, but hardware debugging cannot
start until the probe connection works.

### Boot-time measurement

When measuring boot time for this Zephyr image, exclude flash/programming time
and measure only from a hardware reset to a usable serial shell prompt.

The manual procedure below is wrapped in:

```bash
scripts/measure_zephyr_boot_time.sh
```

Useful options:

```bash
scripts/measure_zephyr_boot_time.sh --samples 10
scripts/measure_zephyr_boot_time.sh --json
scripts/measure_zephyr_boot_time.sh --verbose
```

The script assumes the board is already running the image you want to measure.
If you want to re-program the current Zephyr app first, run
`scripts/flash_zephyr_hello_shell.sh --flash-only --skip-validate` before
measuring.

Before measuring, the script now terminates same-user processes that already
hold the UART port, such as `minicom`. If the port is still busy afterward, it
fails with a clear error showing the blocking process.

The measurement used in this repo is:

1. Program the image once so the board is already running the expected build.
2. Issue a hardware reset with STM32CubeProgrammer:

   ```bash
   PROG=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI
   "$PROG" -c port=SWD mode=UR reset=HWrst freq=8000 -hardRst
   ```

3. Re-open the ST-LINK VCP at `115200 8N1` and capture the UART stream.
4. Treat the first usable `uart:~$` prompt as "boot complete". In practice the
   boot banner and prompt can arrive interleaved on the wire, so sending one
   newline probe after the banner is acceptable if you need the shell to redraw
   a clean prompt.
5. Immediately run `kernel uptime`. That value is the board-side boot time from
   Zephyr's point of view, while the external stopwatch includes small host-side
   reset and serial reopen overhead.
6. Repeat the reset several times and report mean/min/max rather than relying
   on a single sample.

Using that method on this setup, five reset cycles produced:

- host-observed `hard reset -> uart:~$`: mean `813.7 ms`, min `810.3 ms`, max
  `820.0 ms`
- board-reported `kernel uptime` when the prompt was usable: mean `748.4 ms`,
  min `737 ms`, max `764 ms`

The second number is the better estimate of firmware boot time. The difference
between the two numbers is mostly host-side reset command and serial reconnect
latency.

### Ethernet validation

The Zephyr app also enables the on-board Ethernet MAC/PHY with a static IPv4
address:

- board: `192.168.55.2/24`
- host: `192.168.55.100/24`

Assign the host address to the NIC that is cabled to the board (replace
`<nic>`), and make sure no other interface — especially a leftover native_sim/
QEMU `zeth` TAP — holds the same address or subnet route:

```bash
ip -brief addr | grep 192.168.55.        # .100 must be on <nic> only
sudo ip addr del 192.168.55.100/24 dev zeth   # only if zeth holds it
sudo ip addr add 192.168.55.100/24 dev <nic>
ip route get 192.168.55.2                # must show "dev <nic>"
```

With the cable connected and link up, the serial shell should show the Ethernet
interface and the static address:

```text
uart:~$ net iface
...
Interface ... (Ethernet)
...
IPv4 unicast addresses (max ...):
        192.168.55.2 manual preferred infinite
IPv4 netmask : 255.255.255.0
```

The board should be able to ping the host:

```text
uart:~$ net ping -c 1 192.168.55.100
PING 192.168.55.100
... bytes from 192.168.55.100 to 192.168.55.2 ...
```

And the host should be able to ping the board:

```bash
ping -c 1 192.168.55.2
```

`scripts/flash_zephyr_hello_shell.sh` now includes those Ethernet checks during
validation. Override the defaults with `--host-ipv4`, `--board-ipv4`, and
`--board-netmask` if your lab network uses different addresses.

### Ethernet bandwidth testing with iperf

Use the upstream Zephyr `samples/net/zperf` firmware with **host iperf v2**
(`iperf`, not `iperf3`). There is no Linux or iperf executable on the board:
zperf runs on Zephyr and is controlled through the UART shell.

The benchmark temporarily replaces the dashboard firmware, so camera, speech,
and other application tasks do not run during measurement. These results measure
standalone Zephyr networking, not dashboard throughput under application load or
the absolute hardware maximum.

From the repository root:

```bash
scripts/flash_zephyr_zperf.sh --build-only  # Optional: build without flashing
scripts/flash_zephyr_zperf.sh              # Build, save dashboard HEX, flash zperf
scripts/flash_zephyr_zperf.sh --help
```

The script uses the Zephyr SDK ARM toolchain and a separate build directory,
`build/zephyr-bandwidth`. Before flashing, it saves the dashboard build's HEX
once to `build/zperf-dashboard-backup/zephyr/zephyr.hex`. Ensure
`build/zephyr-hello-shell/zephyr/zephyr.hex` matches the dashboard firmware you
want to restore. An existing backup is retained, not refreshed automatically.
The script builds/flashes only; run the measurements separately as below.

Connect the host and board by Ethernet. The benchmark configuration uses board
address `192.168.55.2/24` and host address `192.168.55.101/24`. Confirm
`ip route get 192.168.55.2` selects the board-facing NIC, not a leftover `zeth`
interface. Open the board's ST-LINK UART (`/dev/ttyACM0` on the test host) at
115200 baud, with only one serial client accessing the port.

**Host to board:** start the receivers in the board's UART shell:

```text
net iface
zperf tcp download 5001
zperf udp download 5001
```

Then run on the host:

```bash
# Repeat the TCP command three times for separate 10-second measurements.
iperf -c 192.168.55.2 -B 192.168.55.101 -p 5001 -t 10 -f m

# Repeat UDP at 20M, 50M, and 90M; inspect receiver throughput and packet loss.
iperf -u -c 192.168.55.2 -B 192.168.55.101 -p 5001 -t 10 -b 90M -l 1470 -f m
```

**Board to host:** start a TCP receiver on the host:

```bash
iperf -s -B 192.168.55.101 -p 5001 -i 10 -f m
```

Issue this command three times in the board's UART shell, waiting for each
10-second transfer to complete:

```text
zperf tcp upload 192.168.55.101 5001 10 1064
```

For UDP, stop the host TCP receiver and start:

```bash
iperf -s -u -B 192.168.55.101 -p 5001 -i 10 -f m
```

Then issue in the board's UART shell:

```text
zperf udp upload 192.168.55.101 5001 10 1064 90M
```

The 2026-10-22 run used the same sequence, but requested a 1460-byte TCP upload
payload. zperf warned and capped it to 1064 bytes; the commands above use that
effective size explicitly. The benchmark uses 32 KiB TCP send/receive windows.

| Measurement | Host to board | Board to host |
| --- | --- | --- |
| TCP, three 10-second runs, host iperf reporting | 94.6–94.7 Mbps | 78.2–78.5 Mbps |
| UDP, highest tested offered rate | 93.3 Mbps, 0.25% loss | 77.3 Mbps, zero loss |

The link negotiated 100 Mbps full duplex. For comparison, the raw line rate is
12.5 MB/s per direction; ideal TCP payload throughput with MTU 1500 is about
95 Mbps before TCP options and application overhead. The UDP `-b` argument is
the requested rate, not proof of achieved throughput; use the receiver report.
Local measurement logs are under `build/zephyr-bandwidth/results/`.

**Restore the dashboard after testing:**

```bash
scripts/flash_zephyr_zperf.sh --restore
```

This **reflashes the saved HEX from the host to the STM32 over ST-LINK/SWD**.
STM32CubeProgrammer writes internal flash, verifies the data, and resets the
board so the dashboard starts again. It does not rebuild the application or
switch an on-device rollback slot. The backup is a host-side firmware file,
not a snapshot of runtime RAM or application state. Stop the host iperf receiver
when finished.

### Standalone latency testing with zyclictest

For an idle M7 latency baseline without dashboard, USB, networking, or speech
workloads, use the upstream Zephyr shell sample with `zyclictest` enabled.
Like the iperf helper, this temporarily replaces the dashboard firmware:

```bash
source "$HOME/.venvs/zephyr-4.4/bin/activate"  # Existing Python 3.12+ Zephyr environment
scripts/flash_zephyr_zyclictest.sh --build-only  # Optional: build without flashing
scripts/flash_zephyr_zyclictest.sh              # Build, save dashboard HEX, flash
scripts/flash_zephyr_zyclictest.sh --help
```

The helper uses the Zephyr SDK, a separate `build/zephyr-zyclictest/` directory,
and saves the dashboard HEX once to
`build/zyclictest-dashboard-backup/zephyr/zephyr.hex`. Ensure
`build/zephyr-hello-shell/zephyr/zephyr.hex` matches the firmware you want restored.
Override these paths with `ZYCLICTEST_BUILD_DIR`, `ZYCLICTEST_DASHBOARD_BUILD_DIR`,
and `ZYCLICTEST_BACKUP_DIR`; use a new backup directory to preserve a newer
dashboard build. Linked worktrees also need `ZEPHYR_BASE` pointing to the shared
Zephyr checkout and `ZYCLICTEST_DASHBOARD_BUILD_DIR` pointing to the dashboard build.

On the ST-LINK UART shell at 115200 baud, run a roughly 10-second measurement:

```text
uart:~$ zyclictest start -i 400 -p -11 -l 25000
```

Wait for `Finished thread with count: 25000`, then retrieve IRQ and thread
minimum/average/maximum latency in microseconds:

```text
uart:~$ zyclictest stop -q
```

Use `zyclictest stop` instead for the full histogram, or
`zyclictest stop -c -q` to cancel a loop-mode run early. Always stop the previous
run before starting another, and do not issue `stop` before the first run.
The benchmark enables tickless scheduling with 1 MHz kernel timekeeping and
M7 instruction/data caches. Histogram range is 0-999 us; nonzero overflow counts
mean samples exceeded that range, making histogram-derived averages unreliable.
Choose an interval at least twice the expected worst-case latency.
This is a baseline measurement, not a guaranteed worst-case bound; to measure
latency under real application load, use the dashboard's existing `zyclictest`
command instead.

On NUCLEO-H755ZI-Q with Zephyr v4.4.0 (2026-10-25), the 25,000-cycle run above
reported maximum IRQ latency **3 us**, maximum thread latency **6 us**, and
zero overflows. A separate 2,500-cycle run also produced the histogram.
These short idle measurements are observations, not worst-case guarantees.

Restore the saved dashboard image over ST-LINK/SWD:

```bash
scripts/flash_zephyr_zyclictest.sh --restore
```

Restore writes and verifies the saved host-side HEX and resets the board.
It does not restore runtime RAM state. No automatic benchmark or UART validation
is performed by the helper.

#### Measuring latency under webserver workload

The M7 webserver application already enables `CONFIG_ZYCLICTEST_SHELL=y`,
`CONFIG_TICKLESS_KERNEL=y`, and `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000` in
`apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`.
No extra application code is needed.

Restore the saved dashboard with the command above, or build and flash the
current application from the repository root:

```bash
source "$HOME/.venvs/zephyr-4.4/bin/activate"
scripts/flash_zephyr_hello_shell.sh --build-dir "$PWD/build/zephyr-hello-shell"
```

Start measurement on the UART shell:

```text
uart:~$ zyclictest start -i 400 -p -11 -l 25000
```

During the next approximately 10 seconds, exercise the web dashboard and the
USB, network, or speech workload being evaluated. Wait for
`Finished thread with count: 25000`, then print results and clean up:

```text
uart:~$ zyclictest stop -q
```

Compare IRQ and thread min/avg/max latency and overflow counts against the
standalone baseline using the same interval, priority, and loop count.
Priority `-11` probes a high-priority cooperative thread; it does not measure
HTTP response time or the scheduling latency of every application thread.
The standalone results above do not validate latency under dashboard load.

**Always stop a completed run before starting another.** Loop completion does
not release the test's thread handle. A repeated `start` can report
`zyclictest already running` (`-16`, busy). In this upstream version, that
rejected command also resets measurement counters, so subsequent `stop` can
divide by zero. If this happens, reset the board and begin a fresh measurement
rather than trusting the old results.

### HTTP validation

With Ethernet link up, the board also serves HTTP on the same static IPv4
address:

- browser dashboard: `http://192.168.55.2/`
- board/network status: `http://192.168.55.2/api/v1/status`
- current button/LED/mouse/die-temperature state: `http://192.168.55.2/api/v1/io`
- low-latency USB mouse snapshot: `http://192.168.55.2/api/v1/mouse`
- LED control: `POST http://192.168.55.2/api/v1/leds`
- dashboard assets: `http://192.168.55.2/styles.css` and `http://192.168.55.2/app.js`

The dashboard uses a high-contrast dark color scheme and JavaScript polling to
show:

- live board/network status
- current user-button state
- USB mouse movement, wheel distance, held buttons, and click counts
- live LED state cards
- per-LED `On`, `Off`, and `Toggle` controls from the browser

Example host-side checks:

```bash
curl --fail http://192.168.55.2/
curl --fail http://192.168.55.2/api/v1/status
curl --fail http://192.168.55.2/api/v1/io
curl --fail \
  -X POST \
  -H 'Content-Type: application/json' \
  --data '{"led":"green","action":"on"}' \
  http://192.168.55.2/api/v1/leds
curl --fail \
  -X POST \
  -H 'Content-Type: application/json' \
  --data '{"led":"green","action":"off"}' \
  http://192.168.55.2/api/v1/leds
```

The HTTP API is scoped to built-in board I/O only in this first version. Yellow
and red remain under their background blink threads, so HTTP writes to those
LEDs are observable immediately but may be superseded by the next blink toggle.
Green is the stable LED used by the validation flow for deterministic write/read
checks. The dashboard reflects that same behavior: yellow and red are live but
can change again on their own because of the background blink threads.

Connect a boot-protocol USB mouse directly to USER USB CN13; USB host processing
starts automatically at boot (`usbhost pause`/`resume` on the ST-LINK UART
toggle it for debugging). The dashboard's USB mouse card shows a gliding cursor
with a glowing trail that fades over about 1.5 s (cyan, or green/red/yellow
while left/right/middle is held),
moving within its pad, held left/right/middle buttons, press counts, wheel
total, and report count. These values also appear under `io.mouse` in
`/api/v1/io` and `/api/v1/status`. The dashboard polls `/api/v1/io` about
every 100 ms after each response, which drives the button, LED cards, and
mouse card, so every 400 ms / 700 ms blink toggle is shown. Network status and
uptime use a separate 1.5-second `/api/v1/status` refresh. Movement and press
counts accumulate between page updates; they reset on mouse reconnect.
This displays input from the board, not a host-computer mouse or keyboard.

The default worker, introduced by the `feature/usb-event-worker` experiment, uses
FIFO notifications and immediate IRQ/SOF wake-ups rather than a continuous
50 us busy-wait. Hardware IRQ servicing and microphone resubmission remain in
the ISR. `usbhost status` shows scheduling mode and queue/runtime counters;
`usbhost workerstats` clears measurement counters without erasing overflow
evidence. Original polling is available through `usb-poll-baseline.conf`;
`usbhost pollmode on` is a separate, intentionally polling diagnostic.
See the [A/B experiment report](docs/experiments/usb-event-worker/README.md)
for hardware results, clock fix, limitations, and sanitized raw logs.

The board has no dedicated temperature sensor, so the dashboard's "MCU die
temp" card shows the STM32H755 internal die sensor (ADC3 channel 18,
factory-calibrated at 30 °C / 110 °C; Zephyr node `die_temp`). It is exposed
as `io.temperature` (`{"available":true,"source":"mcu_die","celsius":34.2}`)
and sampled at most once per second. This is chip junction temperature, so it
reads above ambient.

Device status values briefly glow (~1 s) whenever they change, so updates such
as a temperature tick or a link-state change stand out. Uptime is excluded
because it changes on every refresh.

`scripts/flash_zephyr_hello_shell.sh` now validates the HTTP endpoints in
addition to the existing boot, shell, and Ethernet checks.

### Camera stream (looping MJPEG)

The board also serves a short, looping video clip as a `multipart/x-mixed-replace`
MJPEG stream on a dedicated TCP port, isolated from the port-80 HTTP
dashboard so viewing the stream never blocks LED/board-IO requests:

- stream URL: `http://192.168.55.2:8081/`

Open that URL directly in VLC (`Media > Open Network Stream`) or with
`ffplay http://192.168.55.2:8081/` to view the loop. The dashboard
(`http://192.168.55.2/`) also has a Camera panel with a Show/Hide button
(hidden by default). It embeds the same port-8081 stream, so the video still
comes from its own thread rather than the port-80 server. The stream serves
one viewer at a time: hide it in the dashboard before opening it in VLC.
Hiding disconnects the browser so the next viewer can connect.

The network stack is sized for this (`CONFIG_NET_MAX_CONTEXTS=12`,
`CONFIG_NET_MAX_CONN=12`). With the default 6 TCP contexts, an active stream
used up the pool and the dashboard intermittently showed
`Request to /api/v1/io failed (0)`.

The frame data comes from `apps/zephyr-hello-shell/src/camera_frames.c`,
generated offline from a source video with:

```bash
python3 scripts/convert_video_to_mjpeg_frames.py \
  --input ~/Downloads/3842258-uhd_3840_2160_30fps.mp4 \
  --output-dir apps/zephyr-hello-shell/src \
  --width 320 --height 180 --fps 8 --duration 6 --start 0 \
  --jpeg-quality 7 --flash-budget-bytes 524288 --var-prefix camera_frame
```

Re-run this command (and rebuild/reflash) to change the clip. The script
fails with a clear error if the encoded frame data would exceed the
`--flash-budget-bytes` limit.

### USB microphone (listen in Edge)

A USB headset microphone on USER USB **CN13** can be heard live in the browser.
Tested with **Logitech H570e Stereo** (`046d:0a56`): UAC1, full-speed, mic on
IF1 alt1, isochronous EP `0x81`, captured as 48 kHz / 2 ch / 16-bit
(~192 KB/s). The firmware parses the device's real configuration descriptor
and picks the best supported mic format; other UAC1 mics with 16/24-bit PCM
should work but are untested.

Use:

1. Plug the headset into CN13 (USB mouse and headset are swappable at runtime;
   only one device at a time, no hub support yet).
2. Open `http://192.168.55.2/` in Edge. The **USB microphone (CN13)** card
   shows format, input level, drops/errors.
3. Click **Start listening**. Audio plays through the computer's selected
   output (not the headset speaker). Measured delay is about 0.5–1 s.

Endpoints:

- `GET http://192.168.55.2/api/v1/mic` — status JSON, e.g.
  `{"attached":true,"ready":true,"session":1,"format":{"sample_rate_hz":48000,"channels":2,"bits_per_sample":16},"level":4,"packets":16986,"drops":0,"errors":0,"missed":0,"reason":"streaming"}`.
  `drops` counts PCM overwritten in the 250 ms board ring while nobody listens.
- `GET http://192.168.55.2:8082/` — raw stream: 16-byte header
  (`"MIC1"`, rate u32 LE, channels u16 LE, bits u16 LE, session u32 LE)
  followed by signed little-endian interleaved PCM. One listener at a time:
  `409` if busy, `503` if no supported mic is attached.

Quick check without a browser:

```bash
curl -s --max-time 2 http://192.168.55.2:8082/ -o mic.bin; head -c 4 mic.bin  # MIC1
```

UART shell diagnostics: `usbhost audio` (live counters), `usbhost config`
(raw configuration descriptor dump).

Build note: correct isochronous capture needs an STM32 HCD HAL fix (drain
the RX FIFO before channel-halt callbacks). The app's CMake applies
`patches/stm32h7-hcd-rx-before-hcint.patch` to a private copy of
`stm32h7xx_hal_hcd.c` in the build directory, leaving the shared Zephyr HAL
untouched; configuration fails if the patch no longer applies. See
`patches/README.md`.

Troubleshooting:

- Card stays disconnected after replug: run `usbhost vbuscycle` on the UART
  shell (rare enumeration stall, also present before the mic work).
- `busy` in the card: another tab/player holds port 8082; stop it first.
- Not supported: headset speaker output, recording, browser-to-board audio,
  USB hubs.

### Speech AI (English keywords, on the board)

The **Speech AI** dashboard card recognizes English **yes, no, up, down, left,
right, stop** from the Logitech H570e microphone on CN13. TensorFlow Lite Micro runs
the trained int8 `tiny_conv` model (`models/speech_keywords.tflite`) inside a
dedicated M7 Zephyr thread. No training during builds, PC inference, cloud
service, or Internet connection is needed.
The browser only displays results. Hebrew and general speech transcription
are not supported.

#### Model summary

| Item | Value |
|---|---|
| Model | TensorFlow `micro_speech` `tiny_conv` CNN, int8 quantized, v3 |
| Classes | 9: silence, unknown, yes, no, up, down, left, right, stop |
| Training data | Google Speech Commands v0.02 (`scripts/train_speech_keywords.sh`) |
| Accuracy | 80.22% held-out int8 clip classification (N=3508); not a false-trigger rate |
| Runtime | TensorFlow Lite Micro + CMSIS-NN kernels, pure software on Cortex-M7 (480 MHz) |
| Accelerator | None (no NPU on STM32H755); M4 core stays off |
| Model storage | Flash bank 2 at `0x08100000` (frontend + classifier) |
| Thread | Dedicated Zephyr thread, priority 11 (below USB worker at 10) |
| Compute | About 53 ms per 100 ms inference on the current build (~50% of M7) |

How it executes: everything runs as software on the Cortex-M7 core. The M7
includes the Armv7E-M DSP extension, i.e. SIMD instructions inside the CPU
itself (one 32-bit register holds 2 x 16-bit or 4 x 8-bit values; e.g. `SMLAD`
does two multiply-accumulates per instruction). CMSIS-NN is Arm's library of
neural-network kernels (convolution, fully connected, etc.) written to use those
instructions. TensorFlow Lite Micro is the interpreter that walks the model
graph and calls the CMSIS-NN kernel for each layer instead of plain C loops,
giving roughly 2-4x speedup over plain C on the same core. The int8 model maps
directly onto these integer SIMD instructions; the FPU is barely used.

**CMSIS-NN is a software library, not a separate hardware accelerator.** It is
compiled into the firmware and executes on the same Cortex-M7 CPU:

```text
TensorFlow Lite Micro -> CMSIS-NN functions -> Cortex-M7 DSP/SIMD instructions
```

Its execution therefore counts toward `speech_ai` CPU usage. A separate NPU
would execute neural-network operations outside the CPU; the STM32H755 has no
NPU, and this application does not use its separate M4 core for inference.

Per inference (every 100 ms, called a "frame" by the acceptance settings):

1. Capture 48 kHz stereo 16-bit from the USB microphone.
2. Low-pass filter the left channel and decimate to 16 kHz.
3. Frontend model turns the last ~1 s of audio into 49 x 40 features
   (30 ms windows, 20 ms stride).
4. Classifier outputs 9 scores (0-1, not calibrated probabilities).

Because every frame scores a sliding ~1 s window, consecutive frames overlap by
about 90%. A spoken word stays fully inside the window for several frames, so
word alignment within a frame does not matter. Words spoken less than ~1 s
apart share a window and their scores mix; the acceptance timing settings
below control how quickly back-to-back words can be accepted.

Recognition starts automatically with a ready microphone. Open
`http://192.168.55.2/`, speak near the headset microphone, and watch **Detected**.
The current model class and model score are raw predictions, not accepted
commands or measured accuracy. By default, acceptance requires a winning
keyword score of at least 0.8 over three consecutive 100 ms decisions; repeated
events are suppressed until a non-keyword interval. Use **Detection threshold**
on the Speech AI card to change the cutoff immediately to an integer from 50%
through 100%. A cutoff change clears any partial confirmation streak, does
not change the displayed raw model score, and resets to 80% after a board
reboot. The same card also sets **Frames above threshold** (1–10, default 3),
**Re-arm quiet** (0–2000 ms of non-keyword time before another event, default
300) and **Min gap** (0–5000 ms between accepted events, default 1000); these
also reset to defaults after a reboot. All LEDs keep their existing behavior. **Start listening** remains
optional and works independently.

The Speech AI card shows a voice joystick: up/down/left/right latch the knob,
stop centers it, yes/no press the matching button for 0.7 s. Joystick state
lives in the browser only.
In a first spoken check on the board (v3 model, 80% threshold) the joystick
and YES/NO buttons followed accepted events correctly; some spoken words were
missed or misrecognized. This is a user observation, not a scored benchmark.

Capture remains 48 kHz stereo 16-bit. A separate bounded queue feeds the
recognizer without consuming the browser playback ring. The left channel
is low-pass filtered (63-tap FIR, 6 kHz cutoff) and decimated to 16 kHz.
The measured headset channels matched in a room-noise sample. The pinned
upstream frontend uses 30 ms windows, 20 ms stride, and 49 x 40 features.
Other microphone formats remain available to existing capture/playback
but report unsupported format for speech recognition.

`GET /api/v1/speech` returns state/reason, nullable current label/model score,
nullable last accepted event (label, sequence, board uptime), inference and
processing times, processed-window count, capture session, audio gaps, and
dropped recognition frames, plus the active `threshold_percent`,
`accept_frames`, `rearm_ms` and `min_gap_ms`. The dashboard sets them with
`POST /api/v1/speech/settings` and JSON such as
`{"threshold_percent": 80, "accept_frames": 3, "rearm_ms": 300, "min_gap_ms": 1000}`;
any subset of the fields may be sent and the response returns all active values.
Out-of-range or non-integer values fail with HTTP 400 and change nothing. UART command `speech status` shows the same diagnostics. Unplug,
input gaps, or queue overflow clear current predictions
and last event, reset frontend history, and warm up on new contiguous audio.
The normal native_sim/QEMU application reports speech unavailable; it does
not pretend to recognize host or simulated microphone audio.

The Speech AI card also shows **Live attempt** and **Last attempt** keyword
peaks. The board captures each keyword's maximum model score at every inference,
including scores below the detection threshold and trailing frames where
silence/unknown wins; browser polling cannot lose a brief peak within an attempt.
Each row shows its peak, the confirmed active threshold, the difference in
percentage points, and whether the existing command logic accepted that word.
New live peaks briefly glow amber (green for accepted words); reduced-motion
preferences disable the glow. A score above threshold alone is **not** an
accepted command and does not move the voice joystick.

These are model candidate episodes: an attempt starts when any keyword wins,
regardless of threshold, and ends after silence/unknown wins continuously for
300 ms. Silence never erases the completed result, which stays visible while
the next live attempt runs. All seven keyword scores are tracked during each
attempt, even when another class wins. Repeated words without a silence/unknown
break can merge; an utterance classified entirely as silence/unknown cannot
start an attempt. Independent audio-activity detection is not included.

The API exposes nullable `live_attempt` and `last_attempt` objects with
`sequence`, `start_ms`, `end_ms` (null while live), `interrupted`, and seven
`keywords` entries: `{"label":"left","peak":0.674,"accepted":false}`.
Scores range from 0 to 1 and are model outputs, not calibrated probabilities.
Changing threshold recomputes displayed differences without resetting peaks or
changing historical acceptance flags. Audio gaps/errors preserve the current
attempt as interrupted; a new microphone capture session clears both records.
Polling failures keep the displayed numbers but mark them stale.

On the M7 speech build, the dashboard JavaScript shares flash bank 2 sector 0
with the read-only models to keep the application within flash bank 1.
The normal `zephyr.hex` image programs both sections; CM4 remains unused.
Other targets keep the script in normal read-only storage.

Initialize the pinned dependency before a board build:

```bash
git submodule update --init --recursive apps/zephyr-hello-shell/third_party/tflite-micro
scripts/flash_zephyr_hello_shell.sh --build-only
```

The flash script detects the standalone Zephyr SDK's Picolibc and enables
its thread-local storage support when using the `cross-compile` variant.
This is required by the full C++ runtime; a generic cross-toolchain setting
alone does not tell Zephyr which libc is installed. For equivalent manual
builds with this standalone SDK, pass `-DTOOLCHAIN_HAS_PICOLIBC=ON` and
`-DCONFIG_TOOLCHAIN_CROSS_COMPILE_SUPPORTS_THREAD_LOCAL_STORAGE=y`.
Full Zephyr SDK installations using the `zephyr` variant handle this through
their own toolchain configuration.

The default is the seven-keyword direction + stop model (v3), not the earlier
v1 or room-noise experiments. Held-out int8 classification accuracy was 80.22%
(N=3508); this is not a
continuous-stream false-trigger measurement. Whispered "shhhh", mic contact,
and other noises can produce accepted commands. Raising the threshold trades
recall for rejection and is not an established fix. See
[noise investigation](docs/model_debug.md) and [follow-up tasks](TODO.md).

On NUCLEO-H755ZI-Q, classifier and frontend occupy bank 2 sector 0 at
`0x08100000` (47,784 bytes in a 128 KiB region). Inference still runs on M7.
`CONFIG_STM32H7_BOOT_M4_AT_INIT=n` prevents Zephyr from starting M4, but the board
option byte **BCM4 must also remain 0**. Flashing the generated HEX programs both
banks and overwrites any M4 image in that sector. Do not enable M4 or deploy
an M4 image there while this model layout is in use.

Retraining is optional, separate from firmware builds:

```bash
SPEECH_TRAIN_DIR="$HOME/kws-train-stop" scripts/train_speech_keywords.sh
```

The script installs TensorFlow 2.15.1 in a separate venv, downloads Speech
Commands v0.02, trains the stock-noise recipe, and exports a compatible int8
model. Training is stochastic; a new run need not reproduce the committed
model's exact hash or accuracy. Dataset download and training require network
access and substantial disk space/time.

Runtime/model provenance and exact artifact hashes:
[speech model notes](apps/zephyr-hello-shell/third_party/speech-models/README.md).
The runtime/glue uses size optimization to fit alongside the existing camera
and dashboard; the underlying CMSIS-NN library retains speed optimization.

The integrated v1 direction build uses 96.56% of bank-1 flash and 76.84% RAM,
with ARM frontend/classifier arena usage 8768/9076 bytes. With the event USB
worker, speech uses priority 11 (below USB priority 10) so inference cannot
starve notification draining; polling builds retain speech priority 8.
Observed inference was about 27 ms with this scheduling. A 60-second microphone
playback/inference/HTTP trial transferred 11.5 MB, with zero FIFO overflows and
no new recognition gaps/drops. This validates that workload, not noise rejection.

Historical measurements for the original yes/no model on 2026-10-13
(not direction-model benchmarks):

- Firmware uses approximately **98% of the configured 1 MiB flash** and
  **76% of 512 KiB RAM**; flash headroom is small.
- Frontend arena used 8768 bytes; classifier arena used 6948 bytes.
  Each reserves 16 KiB. Inference thread peak stack use in one shell snapshot
  was 2696/8192 bytes.
- Typical observed classifier inference was about **15–21 ms**; a longer
  live-dashboard recording observed up to 32.5 ms. Whole processing chunks
  can take longer under network load.
- Before the event-worker experiment, one `kernel thread list` snapshot reported speech **25%**, USB polling
  **67%**. These are cumulative shares, not a fresh utilization interval.
  `cpu usage 5000` still reported 100% busy because the unchanged low-priority
  USB poll loop busy-waits; inference preempts it. The separate event-worker
  experiment measured about 9% USB thread-attributed time with microphone
  capture instead of about 71% in its polling comparator; see the report above.
- The user reported **9/10 yes and 9/10 no** in a spoken trial. The local
  event recording did not cover the entire user trial, so this is a user
  observation, not an independently scored accuracy benchmark.
- A 60-second microphone/browser playback run transferred about 11.6 MB,
  with no new accepted keywords, no recognition queue drops, no audio gaps,
  and no new USB errors/missed frames. Dashboard responses took at most
  14 ms. The room had ambient noise; this was not a controlled silence or
  unrelated-speech benchmark.
- Camera image, microphone playback, and speech were observed working
  together in a separate 10-second browser check. A longer all-three browser
  check was interrupted; full-duration coexistence remains unverified.
- Physical disconnect/reconnect cleared stale speech state and resumed
  recognition on a new capture session.

Controlled utterance latency, a labeled unrelated-speech false-trigger test,
full-minute camera coexistence, and a physical mouse swap still need dedicated
acceptance checks. This small model is a demo, not a reliable
general-purpose voice-command product; noise, accent, and microphone placement
affect recognition.

Focused tests:

```bash
scripts/test_zephyr_speech.sh
ZEPHYR_BASE="$PWD/vendor/zephyrproject/zephyr" scripts/test_zephyr_speech.sh --model
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" \
  node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
```

`--model` builds a separate native_sim fixture application, checks exact
upstream frontend features and all seven keyword fixtures plus noise inference.
Near-digital-zero silence must not reach the default keyword threshold; it
need not win the silence class. The suite also exercises
worker warmup, gap reset, disconnect, and unsupported-format states.
Fixture audio is test input, not live recognition. The fixture also runs
on the Cortex-M7 `mps2/an500` QEMU target. In an isolated worktree, set
`ZEPHYR_BASE` to the shared Zephyr source path for builds.

## Scripts

The repository's helper scripts are grouped below by what they do. Run them
from the repository root unless their usage says otherwise; scripts that accept
`--help` can show their options.

### Build, run, flash, and debug

| Script | Purpose |
| --- | --- |
| `scripts/flash_zephyr_hello_shell.sh` | Build, flash, and validate the Zephyr hello-shell app on the NUCLEO-H755ZI-Q. |
| `scripts/run_zephyr_hello_shell_native.sh` | Build and run the app with Zephyr `native_sim`, using localhost or TAP networking. |
| `scripts/run_zephyr_hello_shell_qemu.sh` | Build and run the app on QEMU's MPS2 AN500 Cortex-M7 machine. |
| `scripts/flash_zephyr_zperf.sh` | Build and flash Zephyr's zperf network benchmark, and optionally restore the saved dashboard image. |
| `scripts/flash_zephyr_zyclictest.sh` | Build and flash a standalone zyclictest latency benchmark, or restore the saved dashboard image. |
| `scripts/test_flash_zephyr_zyclictest.sh` | Check zyclictest helper actions, backup preservation, and failure handling without hardware. |
| `scripts/flash_gpio_demo.sh` | Build and/or flash the STM32Cube GPIO reference app for both CM7 and CM4. |
| `scripts/debug_zephyr_hello_shell.sh` | Start OpenOCD for ST-LINK debugging of the running M7 app; does not flash. |
| `scripts/gdb_zephyr_hello_shell.sh` | Locate the Zephyr SDK GDB executable and forward its arguments. |

### Capture, measure, and test

| Script | Purpose |
| --- | --- |
| `scripts/capture_zephyr_trace.sh` | Capture the running M7's RAM-resident CTF trace over ST-LINK/OpenOCD and save it with metadata. |
| `scripts/test_capture_zephyr_trace.py` | Test trace capture behavior using fake OpenOCD and GDB tools. |
| `scripts/measure_zephyr_boot_time.sh` | Measure repeated hardware-reset-to-shell-prompt boot times; supports text or JSON output. |
| `scripts/measure_zephyr_boot_time.py` | UART and STM32CubeProgrammer implementation used by the boot-time shell wrapper. |
| `scripts/test_run_zephyr_hello_shell_native.sh` | Test native_sim runner build, run, profile, and error-handling behavior with a fake `west`. |
| `scripts/test_flash_zephyr_hello_shell.sh` | Test the flash flow's CM4 boot-option safety guard and programmer invocation using fakes. |
| `scripts/test_zephyr_speech.sh` | Run host-side speech C tests, optionally building and running native_sim model fixtures with `--model`. |
| `scripts/run_usb_enumerator_tests.sh` | Compile and run the USB host enumerator's C unit tests. |
| `scripts/run_mjpeg_multipart_tests.sh` | Compile and run the multipart MJPEG response C unit tests. |
| `scripts/test_convert_video_to_mjpeg_frames.sh` | Test video-to-C conversion, generated-source compilation, frame count, and flash-budget rejection. |
| `scripts/capture_usb_worker_trial.py` | Capture UART diagnostics for a timed USB worker trial without recording audio; supports event/polling and mouse/microphone modes. |

### Speech and model tools

| Script | Purpose |
| --- | --- |
| `scripts/record_speech_session.py` | Record a labeled board microphone session as raw MIC1 data and matching 16 kHz mono WAV. |
| `scripts/train_speech_keywords.sh` | Train the on-board tiny_conv keyword model using Speech Commands v0.02 and export it for firmware. |
| `scripts/export_speech_keywords.py` | Convert a frozen tiny_conv SavedModel to the firmware-compatible int8 TFLite model. |
| `scripts/dscnn/dump_features.py` | Export quantized micro-frontend keyword features from the Speech Commands dataset. |
| `scripts/dscnn/train_dscnn.py` | Train and evaluate a DS-CNN keyword classifier on exported features, then produce an int8 TFLite model. |
| `scripts/dscnn/convert_static.py` | Convert a saved DS-CNN model to int8 TFLite using representative features and check its input quantization. |

### Document and camera utilities

| Script | Purpose |
| --- | --- |
| `scripts/convert_pdfs_to_md.py` | Convert PDFs to page-structured Markdown using Poppler text extraction and optional OCR of embedded images. |
| `scripts/convert_video_to_mjpeg_frames.py` | Convert a video segment to JPEG frames and generate C source/header arrays for firmware MJPEG streaming. |

### Supporting files

| File | Purpose |
| --- | --- |
| `scripts/zephyr-ctf-scheduling.xml` | Trace Compass XML analysis for visualizing Zephyr task CPU scheduling from CTF traces. |
| `scripts/zperf/benchmark.conf` | Kconfig additions for the zperf network benchmark build. |
| `scripts/zperf/benchmark.overlay` | Devicetree overlay for the zperf benchmark build. |
| `scripts/zyclictest/benchmark.conf` | Enable zyclictest, tickless 1 MHz timekeeping, and M7 caches for standalone latency measurements. |

## STM32CubeIDE reference build

The verified STM32Cube reference baseline is vendored under:

`vendor/reference/stm32cubeh7/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo/STM32CubeIDE`

ST documents that the `NUCLEO-H745ZI-Q` projects are compatible with the
`NUCLEO-H755ZI-Q` board, so this example is used as the local reference.

### Build

```bash
REPO_ROOT="$(git rev-parse --show-toplevel)"
PROJECT_ROOT="$REPO_ROOT/vendor/reference/stm32cubeh7/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo/STM32CubeIDE"
ws=/tmp/stm32cubeide-ws-gpio-demo
/opt/st/stm32cubeide_2.1.1/headless-build.sh \
  -data "$ws" \
  -importAll "$PROJECT_ROOT" \
  -cleanBuild GPIO_Demo_CM7/Debug \
  -cleanBuild GPIO_Demo_CM4/Debug
```

This step generates:

- `"$PROJECT_ROOT/CM7/Debug/GPIO_Demo_CM7.elf"`
- `"$PROJECT_ROOT/CM4/Debug/GPIO_Demo_CM4.elf"`

### Programming

```bash
REPO_ROOT="$(git rev-parse --show-toplevel)"
PROJECT_ROOT="$REPO_ROOT/vendor/reference/stm32cubeh7/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo/STM32CubeIDE"
PROG=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI

sudo "$PROG" -c port=SWD mode=UR reset=HWrst freq=8000 \
  -d "$PROJECT_ROOT/CM7/Debug/GPIO_Demo_CM7.elf" -v \
  -d "$PROJECT_ROOT/CM4/Debug/GPIO_Demo_CM4.elf" -v \
  -hardRst
```

Run the build step first. If the `.elf` files do not exist yet, the flash
command fails.

After `-hardRst`, the application starts automatically.

### Helper script

The same verified STM32Cube flow is wrapped in:

```bash
scripts/flash_gpio_demo.sh
```

Useful options:

```bash
scripts/flash_gpio_demo.sh --build-only
scripts/flash_gpio_demo.sh --flash-only
scripts/flash_gpio_demo.sh --config Release
```

## History
- agent help with git
- verify in hw
- agent serial was good - no need for skill
- pdf to md
- download zrphyr in build
- first build was successful but showed nothing in serial
- root cause: building Zephyr with a local Xilinx/PetaLinux `arm-none-eabi`
  toolchain produced an image that faulted during early UART init
- rebuilding with the official Zephyr SDK `arm-zephyr-eabi` toolchain fixed it
- move app outside
- add leds in cli
- add 2 process leds
- add ip
- add heep server
