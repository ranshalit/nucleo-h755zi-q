# Zephyr 4.4.0 upgrade and zyclictest design

## Goal

Upgrade the repository's Zephyr workspace from v4.2.2 to v4.4.0, the first
release containing the upstream `zyclictest` shell command. Keep the existing
Zephyr-dependent scripts working and make the command available on the
NUCLEO-H755ZI-Q M7 firmware.

## Upgrade and workspace handling

Use a clean v4.4.0 west workspace staged separately from the current
`vendor/zephyrproject` workspace. Preserve the current workspace, including its
local `udc_stm32.c` change, as a rollback copy. Once the v4.4.0 workspace has
passed the required build checks, switch it into the standard
`vendor/zephyrproject` path so existing script defaults remain valid. Do not
discard or overwrite the old workspace.

Pin the documented Zephyr revision to v4.4.0 and update workspace setup
instructions accordingly. Keep the existing Zephyr SDK/toolchain selection
unless a v4.4.0 build demonstrates an incompatibility.

Before carrying forward local changes, compare the new upstream sources with
the archived patches:

- `patches/udc_stm32-sof-enable.patch` matches the current uncommitted change
  in `drivers/usb/udc/udc_stm32.c`. Reapply it only if v4.4.0 does not already
  contain the fix.
- `patches/stm32h7-hcd-rx-before-hcint.patch` targets the STM32H7 HAL module.
  Check whether the v4.4.0 manifest version still needs this patch and whether
  it applies cleanly before applying it.

Do not edit or remove the archived patch files as part of the upgrade.

## zyclictest configuration

Enable `CONFIG_ZYCLICTEST_SHELL=y` in the NUCLEO-H755ZI-Q M7 board-specific
configuration, not the common application configuration. This keeps
`native_sim` and QEMU builds unchanged; their latency results are not
representative of the physical board.

For the M7 target, explicitly enable `CONFIG_TICKLESS_KERNEL=y` and set
`CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000`, following the zyclictest
documentation's recommended minimum resolution. These are new settings: the
current resolved M7 build is tickless but uses Zephyr's 10 kHz default.
Confirm that v4.4.0 and the STM32H755 timer configuration accept these values.
If the 1 MHz setting is unsupported or causes unacceptable effects on the
existing workload, report the concrete constraint rather than silently
falling back to lower-resolution measurement.

The shell usage remains the upstream command interface:

```text
zyclictest start -i <interval-us> -p <priority>
zyclictest stop
```

## Script compatibility

Review all scripts under `scripts/` that use Zephyr source paths, west,
Zephyr build outputs, or Zephyr runtime artifacts. Preserve their existing
arguments, environment overrides, output locations, and expected artifacts.
Make only compatibility fixes required by v4.4.0. Do not change unrelated
PDF, media, speech-data, or STM32Cube utilities.

In particular, validate the app build/flash helper, zperf build helper,
native_sim runner, QEMU runner, trace/debug helpers, and their existing test
scripts. Preserve the current default workspace path after the staged upgrade
is promoted.

## Validation

- Build the M7 application and confirm the resolved configuration enables
  `CONFIG_ZYCLICTEST_SHELL`, tickless mode, and a 1 MHz system tick.
- Build the supported native_sim and QEMU targets without enabling
  zyclictest in their configurations.
- Build the standalone zperf sample used by its helper.
- Run existing Zephyr script regression tests, relevant `--help` paths, and
  shell syntax checks for the Zephyr-dependent scripts.
- Verify that the M7 shell exposes `zyclictest` when board access is
  available. Do not flash hardware as part of the upgrade without explicit
  user authorization.
- Report hardware-only checks separately when no board is available.

## Non-goals

- Do not upgrade beyond v4.4.0 in this change.
- Do not enable zyclictest on simulator profiles.
- Do not modify unrelated scripts or refactor script interfaces preemptively.
- Do not remove the preserved v4.2.2 workspace or archived vendor patches.
