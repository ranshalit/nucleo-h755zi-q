#!/usr/bin/env python3

import json
import os
import pwd
import re
import select
import signal
import statistics
import subprocess
import sys
import termios
import time

PORT = os.environ["ZEPHYR_UART_PORT"]
PROGRAMMER = os.environ["STM32_PROGRAMMER_CLI"]
SWD_FREQ = os.environ["STM32_PROGRAMMER_SWD_FREQ"]
SAMPLES = int(os.environ["ZEPHYR_BOOT_MEASURE_SAMPLES"])
PROMPT_TIMEOUT_SEC = float(os.environ["ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC"])
JSON_OUTPUT = os.environ.get("ZEPHYR_BOOT_MEASURE_JSON") == "1"
VERBOSE_OUTPUT = os.environ.get("ZEPHYR_BOOT_MEASURE_VERBOSE") == "1"
RELEASE_PORT_HOLDERS = os.environ.get("ZEPHYR_BOOT_MEASURE_RELEASE_PORT", "1") == "1"

PROMPT = "uart:~$"
BANNER = "*** Booting Zephyr OS build"
UPTIME_RE = re.compile(r"Uptime:\s+(\d+)\s+ms")
UPTIME_VALUE_RE = re.compile(r"(\d+)\s+ms")
PROMPT_CANON = "uart$"
FIRST_BYTE_VALID_MAX_MS = 250.0
MAX_SAMPLE_ATTEMPTS = 5
PROMPT_CAPTURE_WINDOW_SEC = 0.5


class MeasurementError(RuntimeError):
    pass


def status(message: str) -> None:
    if not JSON_OUTPUT:
        print(f"==> {message}", file=sys.stderr)


def describe_user(uid: int) -> str:
    try:
        return pwd.getpwuid(uid).pw_name
    except KeyError:
        return str(uid)


def describe_process(pid: int) -> str:
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as handle:
            cmdline = handle.read().replace(b"\0", b" ").strip()
        if cmdline:
            return cmdline.decode("utf-8", "replace")
    except OSError:
        pass

    try:
        with open(f"/proc/{pid}/comm", "r", encoding="utf-8", errors="replace") as handle:
            return f"[{handle.read().strip()}]"
    except OSError:
        return "<unknown>"


def list_port_holders() -> list[dict[str, object]]:
    if not os.path.exists(PORT):
        return []

    target = os.path.realpath(PORT)
    holders: list[dict[str, object]] = []
    self_pid = os.getpid()

    with os.scandir("/proc") as proc_entries:
        for entry in proc_entries:
            if not entry.name.isdigit():
                continue

            pid = int(entry.name)
            if pid == self_pid:
                continue

            try:
                uid = entry.stat().st_uid
            except OSError:
                continue

            fd_dir = os.path.join(entry.path, "fd")
            try:
                fd_names = os.listdir(fd_dir)
            except OSError:
                continue

            matches_port = False
            for fd_name in fd_names:
                fd_path = os.path.join(fd_dir, fd_name)
                try:
                    link_target = os.readlink(fd_path)
                except OSError:
                    continue

                if os.path.realpath(link_target) == target:
                    matches_port = True
                    break

            if matches_port:
                holders.append(
                    {
                        "pid": pid,
                        "uid": uid,
                        "user": describe_user(uid),
                        "cmd": describe_process(pid),
                    }
                )

    holders.sort(key=lambda holder: int(holder["pid"]))
    return holders


def format_holders(holders: list[dict[str, object]]) -> str:
    return "; ".join(
        f"pid {holder['pid']} ({holder['user']}): {holder['cmd']}" for holder in holders
    )


def ensure_port_available() -> None:
    holders = list_port_holders()
    if not holders:
        return

    current_uid = os.geteuid()
    releasable = [
        holder
        for holder in holders
        if RELEASE_PORT_HOLDERS and int(holder["uid"]) == current_uid and current_uid != 0
    ]

    if releasable:
        status(f"releasing UART holder(s): {format_holders(releasable)}")
        for holder in releasable:
            try:
                os.kill(int(holder["pid"]), signal.SIGTERM)
            except ProcessLookupError:
                continue
            except OSError as exc:
                raise MeasurementError(
                    f"failed to terminate UART holder pid {holder['pid']}: {exc}"
                ) from exc

        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            remaining = [
                holder
                for holder in list_port_holders()
                if int(holder["uid"]) == current_uid and current_uid != 0
            ]
            if not remaining:
                break
            time.sleep(0.05)

    blocked = list_port_holders()
    if blocked:
        raise MeasurementError(
            f"UART port {PORT} is busy; close other sessions using it and retry: "
            f"{format_holders(blocked)}"
        )


def open_raw_port() -> int:
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def close_quietly(fd: int | None) -> None:
    if fd is None:
        return
    try:
        os.close(fd)
    except OSError:
        pass


def open_raw_port_before(deadline: float) -> int:
    last_error = None
    while time.monotonic() < deadline:
        try:
            return open_raw_port()
        except OSError as exc:
            last_error = exc
            time.sleep(0.02)

    raise MeasurementError(f"failed to open {PORT}: {last_error}")


def read_raw(fd: int, timeout_sec: float) -> bytes:
    ready, _, _ = select.select([fd], [], [], timeout_sec)
    if not ready:
        return b""
    return os.read(fd, 4096)


def write_raw(fd: int, payload: bytes) -> None:
    os.write(fd, payload)
    termios.tcdrain(fd)


def canonicalize_transcript(text: str) -> str:
    return "".join(ch for ch in text.lower() if ch.isalpha() or ch in "$~:")


def prompt_seen(text: str) -> bool:
    canon = canonicalize_transcript(text)
    return PROMPT in text or PROMPT_CANON in canon


def banner_seen(text: str) -> bool:
    canon = canonicalize_transcript(text)
    return BANNER in text or "bootingzephyrosbuild" in canon


def capture_prompt_after_reset(index: int) -> tuple[int, dict[str, object]]:
    t0 = time.monotonic()
    open_deadline = t0 + min(PROMPT_TIMEOUT_SEC, 3.0)
    fd = open_raw_port_before(open_deadline)
    proc = subprocess.Popen(
        [
            PROGRAMMER,
            "-c",
            "port=SWD",
            "mode=UR",
            "reset=HWrst",
            f"freq={SWD_FREQ}",
            "-hardRst",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    transcript = ""
    probe_count = 0
    programmer_output = ""
    seen: dict[str, float] = {}
    last_probe_time = t0
    deadline = t0 + PROMPT_TIMEOUT_SEC
    prompt_deadline = None

    try:
        while time.monotonic() < deadline:
            try:
                data = read_raw(fd, 0.05)
            except OSError:
                close_quietly(fd)
                fd = None
                reopen_deadline = min(deadline, time.monotonic() + 0.5)
                fd = open_raw_port_before(reopen_deadline)
                continue

            if data:
                now = time.monotonic()
                text = data.decode("utf-8", "replace")
                transcript += text

                if "first_byte" not in seen:
                    seen["first_byte"] = now
                    prompt_deadline = min(deadline, now + PROMPT_CAPTURE_WINDOW_SEC)
                if "banner" not in seen and banner_seen(transcript):
                    seen["banner"] = now
                if "prompt" not in seen and prompt_seen(transcript):
                    seen["prompt"] = now
                    break
            else:
                now = time.monotonic()
                if now - t0 > 2.0 and now - last_probe_time > 0.5:
                    write_raw(fd, b"\n")
                    probe_count += 1
                    last_probe_time = now

            if prompt_deadline is not None and time.monotonic() >= prompt_deadline:
                break

        try:
            programmer_output, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            programmer_output, _ = proc.communicate()

        if "first_byte" not in seen:
            snippet = transcript[-400:] if transcript else "<no transcript>"
            raise MeasurementError(
                f"run {index}: banner not observed before timeout; "
                f"transcript tail: {snippet!r}; programmer rc={proc.returncode}"
            )

        return fd, {
            "programmer_output": programmer_output,
            "probe_count": probe_count,
            "command_to_first_byte_ms": round((seen["first_byte"] - t0) * 1000, 1)
            if "first_byte" in seen
            else None,
            "command_to_banner_ms": round((seen["banner"] - t0) * 1000, 1)
            if "banner" in seen
            else None,
            "command_to_prompt_ms": round((seen["prompt"] - t0) * 1000, 1)
            if "prompt" in seen
            else None,
            "transcript": transcript,
        }
    except Exception:
        close_quietly(fd)
        raise


def query_kernel_uptime(index: int, fd: int) -> tuple[int | None, str]:
    last_error = None
    last_output = ""
    for _ in range(3):
        try:
            termios.tcflush(fd, termios.TCIFLUSH)
            write_raw(fd, b"kernel uptime\n")
            output = ""
            settle_deadline = None
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline:
                data = read_raw(fd, 0.05)
                if not data:
                    if settle_deadline is not None and time.monotonic() >= settle_deadline:
                        break
                    continue

                output += data.decode("utf-8", "replace")
                last_output = output
                matches = UPTIME_VALUE_RE.findall(output)
                if matches:
                    settle_deadline = time.monotonic() + 0.2

            matches = UPTIME_VALUE_RE.findall(output)
            if matches:
                return int(matches[-1]), output

            last_error = MeasurementError(
                f"run {index}: timed out waiting for kernel uptime response"
            )
        except Exception as exc:
            last_error = exc

        close_quietly(fd)
        reopen_deadline = time.monotonic() + 3.0
        try:
            fd = open_raw_port_before(reopen_deadline)
        except Exception as exc:
            last_error = exc
            break

    if last_output:
        return None, last_output

    raise MeasurementError(f"run {index}: failed to query kernel uptime: {last_error}")


def summarize(values: list[float | int]) -> dict[str, float]:
    return {
        "mean_ms": round(statistics.mean(values), 1),
        "min_ms": round(min(values), 1),
        "max_ms": round(max(values), 1),
    }


def main() -> int:
    try:
        results = []

        for index in range(1, SAMPLES + 1):
            last_error: Exception | None = None
            for attempt in range(1, MAX_SAMPLE_ATTEMPTS + 1):
                ensure_port_available()
                fd, result = capture_prompt_after_reset(index)
                try:
                    if (
                        result["command_to_first_byte_ms"] is None
                        or float(result["command_to_first_byte_ms"]) > FIRST_BYTE_VALID_MAX_MS
                    ):
                        raise MeasurementError(
                            f"run {index}: implausible first-byte capture "
                            f"{result['command_to_first_byte_ms']} ms"
                        )

                    uptime_ms, uptime_output = query_kernel_uptime(index, fd)
                    break
                except Exception as exc:
                    last_error = exc
                    close_quietly(fd)
                    if attempt == MAX_SAMPLE_ATTEMPTS:
                        raise
                    if not JSON_OUTPUT:
                        print(
                            f"run {index}: discarded capture attempt {attempt} "
                            f"({exc}); retrying"
                        )
                    time.sleep(0.2)
            else:
                raise MeasurementError(f"run {index}: failed after retries: {last_error}")

            close_quietly(fd)

            result["run"] = index
            result["shell_uptime_ms"] = uptime_ms
            result["uptime_output"] = uptime_output
            results.append(result)

            if not JSON_OUTPUT:
                print(
                    f"run {index}: "
                    f"cmd->prompt={result['command_to_prompt_ms'] if result['command_to_prompt_ms'] is not None else 'n/a'}"
                    f"{' ms' if result['command_to_prompt_ms'] is not None else ''}, "
                    f"cmd->banner={result['command_to_first_byte_ms']} ms, "
                    f"shell_uptime={uptime_ms if uptime_ms is not None else 'n/a'}"
                    f"{' ms' if uptime_ms is not None else ''}, "
                    f"probes={result['probe_count']}"
                )
                if VERBOSE_OUTPUT:
                    print("--- transcript ---")
                    print(str(result["transcript"]).strip())
                    print("--- kernel uptime ---")
                    print(uptime_output.strip())

            time.sleep(0.5)

        prompt_values = [
            float(result["command_to_prompt_ms"])
            for result in results
            if result["command_to_prompt_ms"] is not None
        ]
        uptime_values = [
            float(result["shell_uptime_ms"])
            for result in results
            if result["shell_uptime_ms"] is not None
        ]
        banner_values = [
            float(result["command_to_first_byte_ms"])
            for result in results
            if result["command_to_first_byte_ms"] is not None
        ]

        summary = {
            "host_observed_reset_to_prompt": summarize(prompt_values) if prompt_values else None,
            "device_reported_uptime_at_prompt": summarize(uptime_values) if uptime_values else None,
            "host_observed_reset_to_banner": summarize(banner_values) if banner_values else None,
        }

        payload = {
            "port": PORT,
            "samples": SAMPLES,
            "prompt_timeout_sec": PROMPT_TIMEOUT_SEC,
            "summary": summary,
            "results": [
                {
                    "run": result["run"],
                    "probe_count": result["probe_count"],
                    "command_to_first_byte_ms": result["command_to_first_byte_ms"],
                    "command_to_banner_ms": result["command_to_first_byte_ms"],
                    "command_to_prompt_ms": result["command_to_prompt_ms"],
                    "shell_uptime_ms": result["shell_uptime_ms"],
                    **(
                        {
                            "transcript": result["transcript"],
                            "uptime_output": result["uptime_output"],
                        }
                        if VERBOSE_OUTPUT
                        else {}
                    ),
                }
                for result in results
            ],
        }

        if JSON_OUTPUT:
            print(json.dumps(payload, sort_keys=True))
        else:
            print("==> summary")
            host_summary = summary["host_observed_reset_to_prompt"]
            device_summary = summary["device_reported_uptime_at_prompt"]
            banner_summary = summary["host_observed_reset_to_banner"]
            if host_summary is not None:
                print(
                    "host-observed hard reset -> prompt: "
                    f"mean {host_summary['mean_ms']} ms, "
                    f"min {host_summary['min_ms']} ms, "
                    f"max {host_summary['max_ms']} ms"
                )
            else:
                print("host-observed hard reset -> prompt: unavailable")
            if device_summary is not None:
                print(
                    "device-reported kernel uptime at usable prompt: "
                    f"mean {device_summary['mean_ms']} ms, "
                    f"min {device_summary['min_ms']} ms, "
                    f"max {device_summary['max_ms']} ms"
                )
            else:
                print("device-reported kernel uptime at usable prompt: unavailable")
            if banner_summary is not None:
                print(
                    "host-observed hard reset -> first boot banner byte: "
                    f"mean {banner_summary['mean_ms']} ms, "
                    f"min {banner_summary['min_ms']} ms, "
                    f"max {banner_summary['max_ms']} ms"
                )
        return 0
    except MeasurementError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
