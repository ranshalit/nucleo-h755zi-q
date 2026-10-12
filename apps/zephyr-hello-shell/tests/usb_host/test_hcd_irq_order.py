#!/usr/bin/env python3
"""Guard RX FIFO drain ordering before HAL channel-completion callbacks."""

import os
from pathlib import Path


root = Path(__file__).resolve().parents[4]
zephyr_base = os.environ.get("ZEPHYR_BASE")
if zephyr_base:
    hal_path = (
        Path(zephyr_base).parent
        / "modules/hal/stm32/stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c"
    )
else:
    hal_path = (
        root
        / "vendor/reference/stm32cubeh7/Drivers/STM32H7xx_HAL_Driver"
        / "Src/stm32h7xx_hal_hcd.c"
    )
source = hal_path.read_text()
irq_start = source.index("void HAL_HCD_IRQHandler(")
irq_end = source.index("\n}\n", irq_start)
irq = source[irq_start:irq_end]

rx_queue = irq.index("while (__HAL_HCD_GET_FLAG(hhcd, USB_OTG_GINTSTS_RXFLVL))")
channel = irq.index("/* Handle Host channel Interrupt */")
assert rx_queue < channel, "RX FIFO must drain before URB completion callbacks"
assert "HCD_RXQLVL_IRQHandler(hhcd);" in irq[rx_queue:channel]

capture_path = (
    root / "apps/zephyr-hello-shell/src/usb_host/usb_audio_capture.c"
)
capture = capture_path.read_text()
service_start = capture.index("static const char *audio_service(")
service_end = capture.index("\n}\n", service_start)
service = capture[service_start:service_end]
assert "audio_fail(" not in service
assert "USBH_ClosePipe(" not in service
assert "irq_lock()" in service and "irq_unlock(key)" in service

process_start = capture.index("static USBH_StatusTypeDef audio_process(")
process_end = capture.index("\n}\n", process_start)
process = capture[process_start:process_end]
assert "irq_lock()" not in process
assert process.index("audio_service(host, &close_pipe)") < process.index(
    "audio_fail(host, failure)"
)
assert process.index("audio_service(host, &close_pipe)") < process.index(
    "USBH_ClosePipe(host, audio.pipe)"
)

print("PASS: RX FIFO drains before URB callbacks; teardown/logging are unlocked")
