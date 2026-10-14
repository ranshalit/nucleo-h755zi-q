#!/usr/bin/env python3
"""Capture USB worker counters and UART evidence without recording audio."""
import argparse
import re
import sys
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=20)
    parser.add_argument("--mode", choices=("event", "poll-50us"), required=True)
    parser.add_argument("--device", choices=("mouse", "microphone"), required=True)
    parser.add_argument("--reset", action="store_true",
                        help="Reset board via programmer after opening UART")
    parser.add_argument("--programmer", type=Path)
    parser.add_argument("--require-input", action="store_true",
                        help="Require mouse report counter to grow during trial")
    args = parser.parse_args()
    if args.duration < 0:
        parser.error("--duration must be nonnegative")
    if args.reset and args.programmer is None:
        parser.error("--reset requires --programmer")

    args.log.parent.mkdir(parents=True, exist_ok=True)
    escape = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    captured = []
    with args.log.open("w") as output:
        def record(text):
            text = escape.sub("", text).replace("\r", "").replace("\x1b", "")
            captured.append(text)
            output.write(text)
            output.flush()

        record(f"UTC: {datetime.now(timezone.utc).isoformat()}\n"
               f"Trial: mode={args.mode} device={args.device} "
               f"capture_seconds={args.duration}\n")
        with serial.Serial(args.port, 115200, timeout=0.1, exclusive=True) as uart:
            def capture(seconds):
                end = time.monotonic() + seconds
                while time.monotonic() < end:
                    record(uart.read(4096).decode("utf-8", errors="replace"))

            def command(text):
                record(f"\nHOST COMMAND: {text}\n")
                uart.write((text + "\r").encode("ascii"))
                capture(2)

            if args.reset:
                record("\nHOST ACTION: programmer hardware reset\n")
                subprocess.run([str(args.programmer), "-c", "port=SWD", "mode=UR",
                                "-rst", "-run"], check=True, capture_output=True,
                               text=True, timeout=30)
                capture(5)
            uart.write(b"\x03\r")
            capture(1)
            command("usbhost resume")
            capture(3)
            command("usbhost descriptor")
            if args.mode == "event":
                command("usbhost workerstats")
            command("usbhost status")
            record("\nBEGIN MEASUREMENT WINDOW\n")
            capture(args.duration)
            record("\nEND MEASUREMENT WINDOW\n")
            command("usbhost status")
            if args.device == "microphone":
                command("speech status")

    text = "".join(captured)
    for line in text.splitlines():
        if line.startswith(("scheduler:", "events:", "worker:", "worker_runtime:",
                            "state:", "mouse:", "audio:", "vendor_id:", "product_id:")):
            print(line)
    if f"scheduler: {args.mode}" not in text:
        print("ERROR: expected firmware scheduler not observed", file=sys.stderr)
        return 1
    if args.mode == "event" and re.search(r"overflow=[1-9][0-9]*", text):
        print("ERROR: FIFO overflow observed", file=sys.stderr)
        return 1
    states = re.findall(r"^state: (\w+)", text, re.MULTILINE)
    if not states or states[-1] != "class":
        print("ERROR: active USB class not observed", file=sys.stderr)
        return 1
    if args.device == "microphone":
        audio = re.findall(
            r"^audio: connected=(\d+) ready=(\d+).*?packets=(\d+).*?"
            r"errors=(\d+) missed=(\d+)", text, re.MULTILINE)
        if len(audio) < 2 or audio[-1][:2] != ("1", "1"):
            print("ERROR: microphone capture not ready", file=sys.stderr)
            return 1
        if int(audio[-1][2]) <= int(audio[0][2]):
            print("ERROR: microphone packet counter did not advance", file=sys.stderr)
            return 1
        if audio[-1][3:] != audio[0][3:]:
            print("ERROR: microphone errors/missed frames increased", file=sys.stderr)
            return 1
    if args.require_input:
        reports = [int(n) for n in re.findall(r"mouse: reports=(\d+)", text)]
        if len(reports) < 2 or reports[-1] <= reports[0]:
            print("ERROR: no measured mouse report growth", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
