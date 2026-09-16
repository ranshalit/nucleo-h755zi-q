# CubeH7 official USB Host reference (comparison only, not built)

## Why this exists

We wanted to test our board with ST's official, unmodified USB Host example
project ("as-is", outside Zephyr) as the strongest possible isolation step —
eliminating not just the host-stack middleware (already done via the
`stm32-mw-usb-host` A/B test, see `docs/usb_debug.md` Step 19) but also our
own hand-written HAL/board bring-up code.

**Finding: no such stock example exists for our board.** Checked the full
`STMicroelectronics/STM32CubeH7` repository (commit `8ec3113`, 2026-09-16):

- `Projects/NUCLEO-H755ZI-Q` contains only a `readme.txt` pointing at
  `NUCLEO-H745ZI-Q` for compatible examples.
- `Projects/NUCLEO-H745ZI-Q` has no `USB_Host` application at all (no
  `Applications/USB_Host` directory).
- Across the entire repo, `Applications/USB_Host/*` examples only exist for
  **EVAL and Discovery boards** (`STM32H743I-EVAL`, `STM32H745I-DISCO`,
  `STM32H747I-DISCO`, `STM32H747I-EVAL`, `STM32H750B-DK`) and for the
  **Nucleo-64 `NUCLEO-H723ZG`** board. None of the Nucleo-144 boards in our
  family (`H743ZI`, `H745ZI-Q`, `H755ZI-Q`) ship an official USB Host
  example.

This is itself informative: ST does not validate/officially support USB
Host mode on the Nucleo-144 USER USB (Micro-AB) connector at all — using it
as Host is a documented-but-unofficial hack requiring the ID-pin jumper and
manual solder-bridge/software workarounds we've already applied. There was
never going to be a true "stock, as-shipped, board-matching" example to
flash and compare against.

## What was done instead

Vendored (source only, no toolchain project files) ST's closest official
reference: `Projects/NUCLEO-H723ZG/Applications/USB_Host/MSC_Standalone`
from the same `STM32CubeH7` repo. H723ZG uses the combo `USB_OTG_HS`
peripheral in embedded full-speed-PHY mode rather than our board's
dedicated `USB_OTG_FS` peripheral, and is a different Nucleo-64 board, so
it cannot be flashed as-is onto our NUCLEO-H755ZI-Q — it is kept here
purely as a **diff reference** for the HAL/HCD configuration.

### Comparison result

Compared `USB_Host/Target/usbh_conf.c` (`HAL_HCD_MspInit`,
`USBH_LL_Init`, `USBH_LL_DriverVBUS`) against our
`apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.c`:

| Setting | ST H723ZG reference | Our H755ZI-Q code | Match? |
|---|---|---|---|
| `Host_channels` | 11 | 8 | differs (channel count only, not a cause) |
| `dma_enable` | DISABLE | DISABLE | ✅ |
| `phy_itface` | `HCD_PHY_EMBEDDED` | `HCD_PHY_EMBEDDED` | ✅ |
| `low_power_enable` | DISABLE | DISABLE | ✅ |
| `vbus_sensing_enable` | DISABLE | DISABLE | ✅ |
| `speed` | `HCD_SPEED_FULL` | `HCD_SPEED_FULL` | ✅ |
| D+/D− GPIO AF | `GPIO_AF10_OTG1_FS`, no pull, very high speed | AF10, no pull, very high speed | ✅ |
| VBUS switch polarity | active-low (state=ON drives pin LOW) | active-low (PD10 LOW = VBUS on) | ✅ |
| PA9 VBUS-sense pin configured in MSP init | **not configured at all** (reference relies solely on `vbus_sensing_enable=DISABLE` and doesn't touch PA9) | configured as no-pull digital input (required — empirically confirmed necessary for `GINTSTS.CMOD` to flip to host mode on our `USB_OTG_FS` core) | difference explained by peripheral: `USB_OTG_HS`-in-FS-mode (H723) vs true `USB_OTG_FS` (H755) apparently have different mode-latch requirements |

No missed initialization step was found. Our configuration matches ST's own
reference wherever the two peripherals overlap; the one difference (PA9)
is a previously-confirmed, hardware-empirical requirement specific to the
`USB_OTG_FS` peripheral used on our MCU, not a deviation from best practice.

## Conclusion

This comparison provides no new leads and does not change the diagnosis:
all firmware/HAL configuration avenues (custom code, ST's official
middleware, and now ST's own official low-level HCD/GPIO/clock reference)
are consistent and correctly configured. The persistent first-SETUP
STALL/ERROR remains most plausibly explained by the physical D+/D− signal
path (see `docs/usb_debug.md` for the recommended continuity check and
protocol-analyzer/scope next steps).
