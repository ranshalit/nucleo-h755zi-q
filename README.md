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

Build both profiles from the repository root:

```bash
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
the host TAP interface at `192.168.55.100/24`. Set up TAP with Zephyr
`net-tools` and configure the host TAP interface before running the TAP
executable; the TAP profile will not work until that interface is available.
The board-side dashboard uses `http://192.168.55.2/`.

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

Connect a boot-protocol USB mouse directly to USER USB CN13; USB host polling
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
