#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PROGRAMMER="${STM32_PROGRAMMER_CLI:-/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI}"
PORT="${ZEPHYR_UART_PORT:-}"
SWD_FREQ="${STM32_PROGRAMMER_SWD_FREQ:-8000}"
SAMPLES="${ZEPHYR_BOOT_MEASURE_SAMPLES:-5}"
PROMPT_TIMEOUT_SEC="${ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC:-15}"
JSON_OUTPUT=0
VERBOSE_OUTPUT=0
RELEASE_PORT_HOLDERS=1

usage() {
  cat <<'EOF'
Usage: scripts/measure_zephyr_boot_time.sh [options]

Measure Zephyr reset-to-prompt time on the attached board.

This script assumes the target image is already flashed. It excludes programming
time from the measurement and instead times repeated hardware resets:

  host timer start -> STM32CubeProgrammer -hardRst -> serial prompt usable

It then immediately runs `kernel uptime` over the serial shell. That command
executes on the board, so it provides the device-side uptime when the prompt
became usable.

Before measuring, the script will terminate same-user processes that already
hold the UART port (for example `minicom`). If the port is still busy after
that, the script exits with a clear error.

Options:
  --samples COUNT       Number of reset cycles to measure. Default: 5.
  --timeout-sec SEC     Per-run timeout waiting for the prompt. Default: 15.
  --port PATH           Override the UART device path.
  --programmer PATH     Override the STM32_Programmer_CLI path.
  --freq KHZ            Override the SWD frequency in kHz. Default: 8000.
  --json                Emit JSON instead of human-readable output.
  --verbose             Include per-run transcript snippets in output.
  --no-release-port     Do not terminate same-user UART holder processes.
  -h, --help            Show this help text.

Environment overrides:
  ZEPHYR_UART_PORT
  STM32_PROGRAMMER_CLI
  STM32_PROGRAMMER_SWD_FREQ
  ZEPHYR_BOOT_MEASURE_SAMPLES
  ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '==> %s\n' "$*"
}

detect_port() {
  if [[ -n "${PORT}" ]]; then
    return
  fi

  local candidate=""
  for candidate in \
    /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*if02 \
    /dev/serial/by-id/*STLINK*if02 \
    /dev/ttyACM0; do
    if [[ -e "${candidate}" ]]; then
      PORT="${candidate}"
      return
    fi
  done

  die "unable to auto-detect a Zephyr UART port; pass --port"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples)
      shift
      [[ $# -gt 0 ]] || die "--samples requires a value"
      SAMPLES="$1"
      ;;
    --timeout-sec)
      shift
      [[ $# -gt 0 ]] || die "--timeout-sec requires a value"
      PROMPT_TIMEOUT_SEC="$1"
      ;;
    --port)
      shift
      [[ $# -gt 0 ]] || die "--port requires a value"
      PORT="$1"
      ;;
    --programmer)
      shift
      [[ $# -gt 0 ]] || die "--programmer requires a value"
      PROGRAMMER="$1"
      ;;
    --freq)
      shift
      [[ $# -gt 0 ]] || die "--freq requires a value"
      SWD_FREQ="$1"
      ;;
    --json)
      JSON_OUTPUT=1
      ;;
    --verbose)
      VERBOSE_OUTPUT=1
      ;;
    --no-release-port)
      RELEASE_PORT_HOLDERS=0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

[[ "${SAMPLES}" =~ ^[1-9][0-9]*$ ]] || die "--samples must be a positive integer"
[[ "${PROMPT_TIMEOUT_SEC}" =~ ^([0-9]+([.][0-9]+)?|[.][0-9]+)$ ]] || die "--timeout-sec must be a positive number"
[[ -x "${PROGRAMMER}" ]] || die "STM32_Programmer_CLI not found: ${PROGRAMMER}"
command -v python3 >/dev/null 2>&1 || die "python3 not found"

detect_port

if [[ "${JSON_OUTPUT}" -eq 0 ]]; then
  log "measuring boot time on ${PORT}"
  log "image must already be flashed; programming time is excluded"
fi

ZEPHYR_UART_PORT="${PORT}" \
STM32_PROGRAMMER_CLI="${PROGRAMMER}" \
STM32_PROGRAMMER_SWD_FREQ="${SWD_FREQ}" \
ZEPHYR_BOOT_MEASURE_SAMPLES="${SAMPLES}" \
ZEPHYR_BOOT_MEASURE_TIMEOUT_SEC="${PROMPT_TIMEOUT_SEC}" \
ZEPHYR_BOOT_MEASURE_JSON="${JSON_OUTPUT}" \
ZEPHYR_BOOT_MEASURE_VERBOSE="${VERBOSE_OUTPUT}" \
ZEPHYR_BOOT_MEASURE_RELEASE_PORT="${RELEASE_PORT_HOLDERS}" \
python3 - <<'PY'
import json
import os
import pwd
import re
import signal
import statistics
import subprocess
import sys
import time

try:
    import serial
    from serial import SerialException
except Exception as exc:
    print(f"error: pyserial is required for boot-time measurement: {exc}", file=sys.stderr)
    sys.exit(2)

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


def serial_open() -> serial.Serial:
    try:
        ser = serial.Serial(PORT, 115200, timeout=0, write_timeout=1, exclusive=True)
    except TypeError:
        ser = serial.Serial(PORT, 115200, timeout=0, write_timeout=1)
        try:
            ser.exclusive = True
        except Exception:
            pass
    try:
        ser.dtr = True
        ser.rts = True
    except Exception:
        pass
    return ser


def try_open_serial():
    try:
        return serial_open()
    except Exception:
        return None


def close_quietly(ser) -> None:
    if ser is None:
        return
    try:
        ser.close()
    except Exception:
        pass


def reopen_serial(deadline: float):
    while time.monotonic() < deadline:
        ser = try_open_serial()
        if ser is not None:
            return ser
        time.sleep(0.02)
    return None


def capture_prompt_after_reset(index: int) -> tuple[serial.Serial, dict]:
    ser = try_open_serial()
    if ser is not None:
        try:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
        finally:
            close_quietly(ser)

    t0 = time.monotonic()
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

    ser = None
    transcript = ""
    probe_count = 0
    last_rx_time = None
    last_tx_time = t0
    seen = {}
    programmer_output = ""
    deadline = t0 + PROMPT_TIMEOUT_SEC

    try:
        while time.monotonic() < deadline:
            if ser is None:
                ser = reopen_serial(deadline)
                if ser is None:
                    break

            try:
                data = ser.read(4096)
            except SerialException:
                close_quietly(ser)
                ser = None
                time.sleep(0.02)
                continue

            if data:
                now = time.monotonic()
                text = data.decode("utf-8", "replace")
                transcript += text
                last_rx_time = now

                if "first_byte" not in seen:
                    seen["first_byte"] = now
                if "banner" not in seen and BANNER in transcript:
                    seen["banner"] = now
                if "prompt" not in seen and PROMPT in transcript:
                    seen["prompt"] = now
                    break
            else:
                now = time.monotonic()
                if (
                    now - t0 > 0.8
                    and now - last_tx_time > 0.15
                    and (last_rx_time is None or now - last_rx_time > 0.08)
                ):
                    try:
                        ser.write(b"\n")
                        ser.flush()
                        probe_count += 1
                        last_tx_time = now
                    except Exception:
                        close_quietly(ser)
                        ser = None
                time.sleep(0.01)

        try:
            programmer_output, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            programmer_output, _ = proc.communicate()

        if "prompt" not in seen:
            snippet = transcript[-400:] if transcript else "<no transcript>"
            raise MeasurementError(
                f"run {index}: prompt not observed before timeout; "
                f"transcript tail: {snippet!r}; programmer rc={proc.returncode}"
            )

        return ser, {
            "programmer_output": programmer_output,
            "probe_count": probe_count,
            "command_to_first_byte_ms": round((seen["first_byte"] - t0) * 1000, 1)
            if "first_byte" in seen
            else None,
            "command_to_banner_ms": round((seen["banner"] - t0) * 1000, 1)
            if "banner" in seen
            else None,
            "command_to_prompt_ms": round((seen["prompt"] - t0) * 1000, 1),
            "transcript": transcript,
        }
    except Exception:
        close_quietly(ser)
        raise


def query_kernel_uptime(index: int, ser: serial.Serial) -> tuple[int, str]:
    last_error = None
    for _ in range(3):
        try:
            ser.reset_input_buffer()
            ser.write(b"kernel uptime\n")
            ser.flush()
            output = ""
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline:
                data = ser.read(4096)
                if data:
                    output += data.decode("utf-8", "replace")
                    match = UPTIME_RE.search(output)
                    if match and PROMPT in output:
                        return int(match.group(1)), output
                else:
                    time.sleep(0.01)
            last_error = MeasurementError(
                f"run {index}: timed out waiting for kernel uptime response"
            )
        except Exception as exc:
            last_error = exc

        close_quietly(ser)
        reopen_deadline = time.monotonic() + 3.0
        ser = reopen_serial(reopen_deadline)
        if ser is None:
            break

    raise MeasurementError(f"run {index}: failed to query kernel uptime: {last_error}")


def summarize(values: list[float | int]) -> dict[str, float]:
    return {
        "mean_ms": round(statistics.mean(values), 1),
        "min_ms": round(min(values), 1),
        "max_ms": round(max(values), 1),
    }


try:
    results = []

    for index in range(1, SAMPLES + 1):
        ensure_port_available()
        ser, result = capture_prompt_after_reset(index)
        try:
            uptime_ms, uptime_output = query_kernel_uptime(index, ser)
        finally:
            close_quietly(ser)

        result["run"] = index
        result["shell_uptime_ms"] = uptime_ms
        result["uptime_output"] = uptime_output
        results.append(result)

        if not JSON_OUTPUT:
            print(
                f"run {index}: "
                f"cmd->prompt={result['command_to_prompt_ms']} ms, "
                f"cmd->banner={result['command_to_banner_ms']} ms, "
                f"shell_uptime={uptime_ms} ms, "
                f"probes={result['probe_count']}"
            )
            if VERBOSE_OUTPUT:
                print("--- transcript ---")
                print(result["transcript"].strip())
                print("--- kernel uptime ---")
                print(uptime_output.strip())

        time.sleep(0.5)

    prompt_values = [result["command_to_prompt_ms"] for result in results]
    uptime_values = [result["shell_uptime_ms"] for result in results]
    banner_values = [
        result["command_to_banner_ms"]
        for result in results
        if result["command_to_banner_ms"] is not None
    ]

    summary = {
        "host_observed_reset_to_prompt": summarize(prompt_values),
        "device_reported_uptime_at_prompt": summarize(uptime_values),
        "host_observed_reset_to_banner": summarize(banner_values)
        if banner_values
        else None,
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
                "command_to_banner_ms": result["command_to_banner_ms"],
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
        print(
            "host-observed hard reset -> prompt: "
            f"mean {host_summary['mean_ms']} ms, "
            f"min {host_summary['min_ms']} ms, "
            f"max {host_summary['max_ms']} ms"
        )
        print(
            "device-reported kernel uptime at usable prompt: "
            f"mean {device_summary['mean_ms']} ms, "
            f"min {device_summary['min_ms']} ms, "
            f"max {device_summary['max_ms']} ms"
        )
        if banner_summary is not None:
            print(
                "host-observed hard reset -> first boot banner byte: "
                f"mean {banner_summary['mean_ms']} ms, "
                f"min {banner_summary['min_ms']} ms, "
                f"max {banner_summary['max_ms']} ms"
            )
except MeasurementError as exc:
    print(f"error: {exc}", file=sys.stderr)
    sys.exit(1)
PY
