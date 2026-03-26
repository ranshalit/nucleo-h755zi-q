# Zephyr build flow for `NUCLEO-H755ZI-Q`

This repo uses the Zephyr board name `nucleo_h755zi_q` for the ST `NUCLEO-H755ZI-Q` board.

Supported Zephyr targets:

- `nucleo_h755zi_q/stm32h755xx/m7`
- `nucleo_h755zi_q/stm32h755xx/m4`

The verified build below was done for the Cortex-M7 target with Zephyr `v4.2.2`.

## Workspace layout

Zephyr was fetched into:

`vendor/zephyrproject`

The Zephyr workspace projects under `vendor/zephyrproject` are tracked as Git
submodules in this repository. After cloning the repo, initialize them with:

```bash
git submodule update --init --recursive
```

The current external-app build output can be generated in:

`/tmp/stm32h7-zephyr-hello-shell`

## Host prerequisites used

The working host setup in this environment was:

- `python3`
- `cmake`
- `dtc`
- `flex`
- `bison`
- `gperf`
- Python packages: `west`, `ninja`, `pyelftools`

The verified working ARM toolchain was the Zephyr SDK `arm-zephyr-eabi` toolchain extracted at:

`/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin`

The previously available Xilinx/PetaLinux `arm-none-eabi` toolchain could compile Zephyr, but the flashed image faulted during early UART init and produced no serial output. Use the Zephyr SDK toolchain for board validation.

## One-time Zephyr fetch

```bash
export PATH="$HOME/.local/bin:$PATH"
mkdir -p vendor
cd vendor
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.2.2 zephyrproject
cd zephyrproject
west update
```

## Install host-side Python tools

```bash
python3 -m pip install --user --disable-pip-version-check west ninja pyelftools
cd vendor/zephyrproject/zephyr
python3 -m pip install --user --disable-pip-version-check -r scripts/requirements.txt
```

## Install the Zephyr SDK ARM toolchain

The session-verified toolchain came from Zephyr SDK release `v1.0.1`.

```bash
mkdir -p "$HOME/.local/zephyr-sdk-1.0.1" "$HOME/.cache/zephyr-sdk"
cd "$HOME/.cache/zephyr-sdk"

curl -L --fail -o hosttools_linux-x86_64.tar.xz \
  https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/hosttools_linux-x86_64.tar.xz
curl -L --fail -o toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz \
  https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz

tar -xf hosttools_linux-x86_64.tar.xz -C "$HOME/.local/zephyr-sdk-1.0.1"
tar -xf toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz -C "$HOME/.local/zephyr-sdk-1.0.1"
```

## Optional: disable M4 boot while debugging M7 console output

To avoid any stale M4 image interfering with single-core M7 bring-up, this session disabled `BCM4` and kept `BCM7` enabled:

```bash
PROG=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI

"$PROG" -c port=SWD mode=UR reset=HWrst freq=8000 \
  -ob BCM4=0 BCM7=1 BOOT_CM7_ADD0=0x800 BOOT_CM4_ADD0=0x810
```

## Build the external hello-shell app for the M7 core

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

## Build artifacts

The verified build produced:

- `/tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.elf`
- `/tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.bin`
- `/tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.hex`

Size check used during verification:

```bash
/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-size \
  /tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.elf
```

Observed result:

```text
   text    data     bss     dec     hex
  44980     244   26190   71414   116f6
```

## Flash the M7 image

The verified flash step used `west flash` with the STM32CubeProgrammer runner:

```bash
export PATH="$HOME/.local/bin:$PATH:/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin:$PATH"

cd vendor/zephyrproject/zephyr
west flash \
  -d /tmp/stm32h7-zephyr-hello-shell \
  --runner stm32cubeprogrammer
```

Observed flash result:

```text
Board       : NUCLEO-H755ZI-Q
Device ID   : 0x450
Opening and parsing file: zephyr.hex
Memory Programming ...
File download complete
RUNNING Program ...
Start operation achieved successfully
```

An additional hardware reset was also performed successfully with `STM32_Programmer_CLI -hardRst`.

## Serial console verification

With the Zephyr SDK-built image on the board, `/dev/ttyACM0` at `115200 8N1` showed:

```text
*** Booting Zephyr OS build v4.2.2 ***
Hello World! nucleo_h755zi_q/stm32h755xx/m7

uart:~$
```

The external app enables the Zephyr shell from its own `prj.conf`, and its
parent-repo `sample.yaml` includes serial-shell validation outside the upstream
Zephyr source tree:

- `apps/zephyr-hello-shell/prj.conf`
- `apps/zephyr-hello-shell/sample.yaml`

Live serial validation also confirmed:

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

During debugging, a temporary UART probe app that printed once per second also
worked correctly on the same `USART3` / ST-LINK VCP path.

## Notes

- The board documentation in Zephyr recommends building per-core target, not just `nucleo_h755zi_q`.
- The verified flow in this repo now covers fetching, building, flashing, and observing console output plus serial shell interaction on the M7 target.
- The most important practical lesson from this session is to use the official Zephyr SDK ARM toolchain for this board.
