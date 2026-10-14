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
python3 -m pip install --user --disable-pip-version-check west ninja pyelftools
cd vendor/zephyrproject/zephyr
python3 -m pip install --user --disable-pip-version-check -r scripts/requirements.txt
```

Install the Zephyr SDK ARM toolchain and use it instead of unrelated local
`arm-none-eabi` toolchains when validating on hardware.

## Zephyr external app

The repo-owned Zephyr application is:

`apps/zephyr-hello-shell`

It keeps the application and shell configuration in this repository instead of
patching the upstream Zephyr source tree under `vendor/zephyrproject/zephyr`.

CPU runtime statistics are enabled for this app. On the UART shell, run
`cpu usage` for CPU busy/idle percentages over 1 second, or
`cpu usage 5000` for a 5-second interval (1–60000 ms). This measures the
M7 Zephyr core, not the separate M4 core. `kernel thread list` also shows
each thread's cumulative execution cycles and CPU percentage since boot;
its `stack size ... (% ...)` lines instead report peak stack use, not CPU.

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

At runtime, the application starts two Zephyr threads: one toggles the yellow
LED (LD2) and the other toggles the red LED (LD3). The green LED (LD1) remains
available through the serial shell commands. The same application now also runs
a plain HTTP server on the board Ethernet interface and exposes built-in board
I/O through both JSON API endpoints and a browser dashboard:

- LEDs `LD1`, `LD2`, `LD3`
- user button `B1`
- board and network status

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

With the programmed image running, `/dev/ttyACM0` at `115200 8N1` should show:

```text
*** Booting Zephyr OS build v4.2.2 ***
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
Zephyr version 4.2.2
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

Recognition starts automatically with a ready microphone. Open
`http://192.168.55.2/`, speak near the headset microphone, and watch **Detected**.
The current model class and model score are raw predictions, not accepted
commands or measured accuracy. By default, acceptance requires a winning
keyword score of at least 0.8 over three consecutive 100 ms decisions; repeated
events are suppressed until a non-keyword interval. Use **Detection threshold**
on the Speech AI card to change the cutoff immediately to an integer from 50%
through 100%. A cutoff change clears any partial confirmation streak, does
not change the displayed raw model score, and resets to 80% after a board
reboot. All LEDs keep their existing behavior. **Start listening** remains
optional and works independently.

The Speech AI card shows a voice joystick: up/down/left/right latch the knob,
stop centers it, yes/no press the matching button for 0.7 s. Joystick state
lives in the browser only.

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
dropped recognition frames, plus the active `threshold_percent`. The dashboard
sets it with `POST /api/v1/speech/settings` and JSON
`{"threshold_percent": 80}`; the response returns the active value. Requests
outside 50–100 or with a non-integer value fail with HTTP 400 and do not alter
the setting. UART command `speech status` shows the same diagnostics. Unplug,
input gaps, or queue overflow clear current predictions
and last event, reset frontend history, and warm up on new contiguous audio.
The normal native_sim/QEMU application reports speech unavailable; it does
not pretend to recognize host or simulated microphone audio.

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
`0x08100000` (43,784 bytes in a 128 KiB region). Inference still runs on M7.
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
