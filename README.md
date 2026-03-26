# General

This is a project of STM32H7 nucleo 144 board (H755ZI-Q NUH755ZIQ$AT3)
It is a playground project for doing various stuff with nucleo board:
building zyphyr, deploying on board, changin pinmux, optimize boot time,
customization of drivers, adding devices etc.

## Configuration
board is connected to host using usb

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

### Flash and run

```bash
REPO_ROOT="$(git rev-parse --show-toplevel)"
PROJECT_ROOT="$REPO_ROOT/vendor/reference/stm32cubeh7/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo/STM32CubeIDE"
PROG=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI

sudo "$PROG" -c port=SWD mode=UR reset=HWrst freq=8000 \
  -d "$PROJECT_ROOT/CM7/Debug/GPIO_Demo_CM7.elf" -v \
  -d "$PROJECT_ROOT/CM4/Debug/GPIO_Demo_CM4.elf" -v \
  -hardRst
```

Run the build step first. If the `.elf` files do not exist yet, the flash command fails.

After `-hardRst`, the application starts automatically.

### Helper script

The same verified flow is wrapped in:

```bash
scripts/flash_gpio_demo.sh
```

Useful options:

```bash
scripts/flash_gpio_demo.sh --build-only
scripts/flash_gpio_demo.sh --flash-only
scripts/flash_gpio_demo.sh --config Release
```

