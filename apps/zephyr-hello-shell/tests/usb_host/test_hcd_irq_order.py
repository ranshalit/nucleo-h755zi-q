#!/usr/bin/env python3
"""Guard RX FIFO drain ordering before HAL channel-completion callbacks."""

import argparse
import json
import os
import subprocess
from pathlib import Path


def check_irq_order(path):
    source = path.read_text()
    irq_start = source.index("void HAL_HCD_IRQHandler(")
    irq_end = source.index("\n}\n", irq_start)
    irq = source[irq_start:irq_end]
    rx_queue = irq.index("while (__HAL_HCD_GET_FLAG(hhcd, USB_OTG_GINTSTS_RXFLVL))")
    channel = irq.index("/* Handle Host channel Interrupt */")
    assert rx_queue < channel, f"RX FIFO must drain before URB callbacks: {path}"
    assert "HCD_RXQLVL_IRQHandler(hhcd);" in irq[rx_queue:channel]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=Path, help="Zephyr build to inspect")
args = parser.parse_args()
root = Path(__file__).resolve().parents[4]
patch = root / "patches/stm32h7-hcd-rx-before-hcint.patch"
default_vendor = root / "vendor/zephyrproject"
if not default_vendor.exists() and root.parent.name == ".worktrees":
    default_vendor = root.parent.parent / "vendor/zephyrproject"
build_dir = args.build_dir or (root / "build/m7-guard")
compiled_db = build_dir / "compile_commands.json"
cached_base = None
cache = build_dir / "CMakeCache.txt"
if cache.exists():
    for line in cache.read_text().splitlines():
        if line.startswith("ZEPHYR_BASE:PATH="):
            cached_base = line.partition("=")[2]
            break
if cached_base and os.environ.get("ZEPHYR_BASE"):
    assert Path(cached_base).resolve() == Path(os.environ["ZEPHYR_BASE"]).resolve(), (
        "ZEPHYR_BASE differs from the HAL used for this build"
    )
zephyr_base = Path(cached_base or os.environ.get("ZEPHYR_BASE", default_vendor / "zephyr"))
module = zephyr_base.parent / "modules/hal/stm32"
relative = Path("stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c")
active_hal = module / relative

if active_hal.exists():
    def patch_checks(*options):
        return subprocess.run(
            ["git", "apply", "--unidiff-zero", *options, "--check", str(patch)],
            cwd=module, capture_output=True, check=False,
        ).returncode == 0

    assert patch_checks("--reverse") or patch_checks(), (
        f"Active HAL differs from supported patch base: {active_hal}"
    )
else:
    assert not (args.build_dir or os.environ.get("ZEPHYR_BASE")), (
        "Cannot validate active HAL without Zephyr module"
    )
    active_hal = (root / "vendor/reference/stm32cubeh7/Drivers/STM32H7xx_HAL_Driver"
                  / "Src/stm32h7xx_hal_hcd.c")
    check_irq_order(active_hal)

if args.build_dir or compiled_db.exists():
    assert compiled_db.exists(), f"Missing compile_commands.json: {compiled_db}"
    commands = json.loads(compiled_db.read_text())
    hcd_sources = [Path(entry["file"]).resolve() for entry in commands
                   if Path(entry["file"]).name == relative.name]
    patched = (build_dir.resolve() / relative).resolve()
    assert hcd_sources == [patched], (
        f"Expected exactly one patched HAL source {patched}; got {hcd_sources}"
    )
    check_irq_order(patched)
elif active_hal.exists() and module.exists():
    # Without a build, the active vendor source may still be unpatched.
    # The build check above validates what actually enters the firmware.
    if patch_checks("--reverse"):
        check_irq_order(active_hal)

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

print("PASS: active HAL patchable; selected HCD IRQ ordering and unlocked teardown/logging")
