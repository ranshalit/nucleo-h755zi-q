# NUCLEO-H755ZI-Q STM32Cube USB HID mouse host

Minimal CM7 bare-metal host for the exact NUCLEO-H755ZI-Q board. It uses the
vendored STM32CubeH7 HAL/CMSIS, the NUCLEO-H745ZI-Q `GPIO_Demo` clock and
board setup, and official ST USB Host Core/HID middleware.

Hardware configuration:

- USER USB CN13: `USB_OTG_FS`, PA11 D-, PA12 D+, AF10
- USB kernel clock: HSI48
- VBUS switch: PD10, active-low; high at boot, low only when USBH starts
- UART: USART3 PD8/PD9, 115200 8N1 through ST-LINK VCP
- Host: CM7-only image at `0x08000000`; current board state is `BCM4=0`
- Intended test device: Dell/PixArt mouse `413c:301a`

The UART log includes ST enumeration messages, VID/PID and HID protocol
selection, and decoded mouse button/X/Y reports. This is more than a raw EP0
probe.

## Middleware provenance

`middleware/` is a source snapshot of the official
`STMicroelectronics/stm32-mw-usb-host` repository at commit
`d013fac0f8b97e8316a79ef946a7dbab4ef4431d`. Its `LICENSE.md` and upstream
README are retained.

## Build

From this directory:

```sh
make clean all
```

Outputs:

```text
build/cube-hid-mouse-cm7.elf
build/cube-hid-mouse-cm7.bin
build/cube-hid-mouse-cm7.map
```

The default compiler is:

```text
/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
```

Override it with `make TOOLCHAIN=/path/to/bin`.

To compare with the HAL used by the Zephyr west checkout (same
8 MHz HSE clock assumption, output kept outside this experiment):

```sh
make HAL_SRC=../../vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/src \
     HAL_INC=../../vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/include \
     BUILD=/tmp/cube-vendor-hal all
```

The `-DHSE_VALUE=8000000U` build flag is required: that HAL's bundled
configuration otherwise assumes a 25 MHz HSE, miscalculates the
system clock as 1.25 GHz, and drives USART3 at about 36.8 kbaud
instead of 115200. The HSE input on this board is 8 MHz.

## Flash and reset

This firmware follows the current board state documented by
`docs/usb_debug.md` Steps 51-53: `BCM4=0`, with CM4 held disabled. It does not
perform the historical HSEM wake handshake. Do not flash a CM4 image and do
not modify option bytes.

With the board connected by ST-LINK, flash only CM7 from this directory:

```sh
PROGRAMMER=/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI

"$PROGRAMMER" -c port=SWD mode=UR reset=HWrst \
  -w build/cube-hid-mouse-cm7.elf \
  -rst
```

Open the ST-LINK VCP at 115200 baud, then connect the mouse directly to CN13.
Expected milestones are `USB Device Connected`, enumeration strings, `Mouse
device found!`, `USB HID active: VID=413c PID=301a type=1`, and `HID mouse
report` lines when the mouse moves or buttons change.

Hardware test on NUCLEO-H755ZI-Q with the Dell/PixArt `413c:301a` mouse:
three fresh resets enumerated and activated the HID class. During a
12-second mouse movement/button test the UART logged 1,147 reports,
including button changes. This validates the STM32Cube reference path
on the same board; it does not fix the Zephyr host's interrupt-IN TXERR.
The HAL-swap build also activated HID on three fresh resets and
logged 1,033 movement/button reports in a 12-second test. Therefore
the HAL version by itself does not explain the Zephyr host failure.
