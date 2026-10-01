# Minimal Zephyr USB mouse isolation

Builds the existing STM32 HAL host adapter, ST USB Host middleware, and
boot-mouse class for `nucleo_h755zi_q/stm32h755xx/m7`. Keeps the UART shell
for `usbhost resume`, `usbhost pause`, and `usbhost status`. Reuses the
production USB host sources and poll cadence without modification.
Networking and the Ethernet MAC/MDIO are disabled; HTTP, camera streaming,
and LED blink threads are not started. `board_io.c` is linked only for the
existing `usbhost rawtest` command; its blink-thread startup is never called.
The early UART/VBUS hook and PD10 VBUS gpio-hog match the normal application.

From the repository root, with `west` and an ARM Zephyr cross-compiler:

```sh
cd vendor/zephyrproject/zephyr
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  CROSS_COMPILE=/path/to/arm-zephyr-eabi/bin/arm-zephyr-eabi- \
  west build -b nucleo_h755zi_q/stm32h755xx/m7 \
    -d /tmp/stm32h7-minimal-mouse ../../../experiments/minimal-zephyr-mouse
STM32_Programmer_CLI -c port=SWD mode=UR \
  -w /tmp/stm32h7-minimal-mouse/zephyr/zephyr.elf -v -rst
```

With the Dell MS116 mouse directly connected to CN13, use the ST-LINK
UART at 115200 baud. After each fresh reset, run `usbhost resume`,
wait for `boot mouse ready`, move/click the mouse, and run `usbhost status`.
Use `usbhost pause` only when finished observing reports.
Only a nonzero `mouse: reports=` count with valid movement/button reports
demonstrates functional input; enumeration alone does not.

In the first four fresh-reset trials the mouse configured every time,
but the first channel-2 interrupt was `TXERR` (`0x80`) every time and
`mouse: reports=0`. Logs: `/tmp/minimal-mouse-reset-{1..4}.log`.
The same failure without the other application services made their
runtime activity unnecessary for reproducing the issue.

The subsequent fast-class-poll fix in the shared production host loop
changed the first interrupt to `NAK` on all four resets of this minimal
image. A 20-second live move/click capture decoded 980 input lines
(1,358 total reports, including 166 lines with buttons) without transfer
errors. The host loop now stays at 50 us in `HOST_CLASS`, while the shell
has higher scheduling priority so it remains responsive. This is a
functional workaround, not proof of the electrical cause of TXERR;
the USB polling thread remains CPU-intensive while the class is active.

Restore the normal application after this diagnostic with
`scripts/flash_zephyr_hello_shell.sh --flash-only --skip-validate`
from the repository root.
