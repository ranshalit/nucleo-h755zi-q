# Zephyr sensor, I2C, and SPI shell commands

## Goal

Expose Zephyr's built-in `sensor`, `i2c`, and `spi` shell commands in the
NUCLEO-H755ZI-Q M7 application. Use upstream Zephyr support only; do not add
application-specific command implementations or invent hardware configuration.

## Design

Enable the required Zephyr shell Kconfig options in
`apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`:

- `CONFIG_SENSOR_SHELL`
- `CONFIG_I2C` and `CONFIG_I2C_SHELL`
- `CONFIG_SPI` and `CONFIG_SPI_SHELL`

The application already enables `CONFIG_SENSOR` and its board overlay already
enables the MCU die-temperature sensor. The board devicetree already enables
I2C1. Keep those existing definitions unchanged. Do not add SPI controller
nodes, pin routing, external sensor nodes, or custom shell commands. The SPI
command is exposed for demonstration and is not expected to operate without a
configured SPI controller.

Keep these options in the M7-specific configuration, leaving common
configuration and `native_sim` unchanged.

## Runtime behavior and errors

The commands and their arguments come directly from Zephyr 4.4. The `sensor`
shell can access configured Zephyr sensor devices, including the existing
on-chip die-temperature sensor. The `i2c` shell operates on configured I2C
controllers, including the existing I2C1 controller. The `spi` shell command
may report that no SPI buses are configured; that is expected and must not be
worked around with an invented bus or device.

## Validation

Build the Nucleo M7 target and confirm the resolved `.config` enables all
three shell options and the required I2C/SPI subsystems. Do not claim runtime
SPI operation without a configured controller.
