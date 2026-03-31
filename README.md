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
- current button/LED state: `http://192.168.55.2/api/v1/io`
- LED control: `POST http://192.168.55.2/api/v1/leds`
- dashboard assets: `http://192.168.55.2/styles.css` and `http://192.168.55.2/app.js`

The dashboard uses a high-contrast dark color scheme and JavaScript polling to
show:

- live board/network status
- current user-button state
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

`scripts/flash_zephyr_hello_shell.sh` now validates the HTTP endpoints in
addition to the existing boot, shell, and Ethernet checks.

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
