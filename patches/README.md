# Vendor Zephyr patches

`vendor/zephyrproject/` is git-ignored (it's a full west workspace, not
part of this repo). Any fix made directly inside it is **lost** if the
workspace is ever re-initialized/updated. This directory archives such
fixes so they can be reapplied.

## udc_stm32-sof-enable.patch

Applies to `vendor/zephyrproject/zephyr/drivers/usb/udc/udc_stm32.c`.

`udc_stm32.c` never wires `CONFIG_UDC_ENABLE_SOF` to ST HAL's
`Init.Sof_enable`, unlike every other in-tree UDC driver. Without it, the
hardware SOF interrupt (`GINTMSK.SOFM`) is never enabled, so isochronous
classes that rely on SOF-driven periodic re-arm of OUT endpoints (e.g.
`usbd_uac2.c`) only ever get a single packet. See `docs/usb_debug.md`
Step 44 for full context.

Apply with:

```
cd vendor/zephyrproject/zephyr
git apply /home/ranshal/project/stm32h7/patches/udc_stm32-sof-enable.patch
```

## stm32h7-hcd-rx-before-hcint.patch

Applies to `vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c`.

`HAL_HCD_IRQHandler()` previously dispatched host-channel completion callbacks
before draining the RX status FIFO. When HCINT/XFRC and RXFLVL were pending
together, a URB_DONE callback could observe the previous transfer length and
buffer contents. The patch drains pending RX FIFO entries before dispatching
channel callbacks, so the audio ISR can consume the completed packet and
resubmit the next isochronous IN URB in the same frame.

Apply with:

```
cd vendor/zephyrproject/modules/hal/stm32
git apply --unidiff-zero /home/ranshal/project/stm32h7/patches/stm32h7-hcd-rx-before-hcint.patch
```
