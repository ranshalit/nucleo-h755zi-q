# Cube USB host middleware under Zephyr

Runs ST's USB Host Core/HID class under minimal Zephyr on
`nucleo_h755zi_q/stm32h755xx/m7`. Compare either the standalone Cube
HCD bridge or the original application STM32 HCD adapter; optionally
substitute the application's boot-mouse class for Cube HID. Connect
the Dell MS116 mouse directly to USER USB CN13 and read the ST-LINK
UART at 115200 baud. The host starts automatically.

From `vendor/zephyrproject/zephyr`, with the ARM Zephyr SDK toolchain:

```sh
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  CROSS_COMPILE=/path/to/arm-zephyr-eabi/bin/arm-zephyr-eabi- \
  west build -b nucleo_h755zi_q/stm32h755xx/m7 \
    -d /tmp/stm32h7-cube-crossover \
    ../../../experiments/zephyr-cube-crossover -- \
    -DCROSSOVER_USE_ZEPHYR_BRIDGE=ON \
    -DCROSSOVER_USE_BOOT_MOUSE_CLASS=ON
STM32_Programmer_CLI -c port=SWD mode=UR \
  -w /tmp/stm32h7-cube-crossover/zephyr/zephyr.elf -v -rst
```

`CROSSOVER_USE_ZEPHYR_BRIDGE=OFF` uses Cube's bridge; the default
`CROSSOVER_USE_BOOT_MOUSE_CLASS=OFF` uses Cube HID. The default host
loop busy-waits 50 us between iterations. To reproduce the
interrupt-IN failure, build with `-DCROSSOVER_SLOW_CLASS_POLL=ON`
to sleep 1 ms in `HOST_CLASS`. `-DCROSSOVER_BURST_CLASS_POLL=ON`
instead tests 99 fast iterations followed by a 1 ms sleep; do not
combine the two timing options. These are diagnostic builds, not
production firmware.

Cube HID with Cube's bridge got first mouse IRQ `NAK` on 4/4 resets
and decoded 1,415 live movement reports. Cube HID with the original
Zephyr bridge got first `NAK` on 3/3 resets and decoded 1,296 live
reports, including buttons. Substituting the application's boot-mouse
class with the Zephyr bridge at 50 us produced first `NAK` on 3/3
resets and 1,030 movement/button lines in 20 seconds. At 1 ms with
the boot-mouse class, first IRQ became `TXERR` on 3/3 resets and
zero reports. Bounded bursts got first `NAK` in 3/4 resets but later
TXERR retries and aborted; one reset stalled during enumeration.
Slowing Cube HID also triggered a later control-transfer failure,
so that variant does not isolate interrupt-IN timing by itself.

The working bridge and class do not require replacing production
middleware. See `docs/usb_debug.md` for the production fast-poll
trial and its limitations. Reflash the normal application with
`scripts/flash_zephyr_hello_shell.sh --flash-only --skip-validate`
after a crossover experiment.
