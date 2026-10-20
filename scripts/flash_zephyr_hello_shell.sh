#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ZEPHYR_BASE="${ZEPHYR_BASE:-${REPO_ROOT}/vendor/zephyrproject/zephyr}"
APP_DIR="${ZEPHYR_APP_DIR:-${REPO_ROOT}/apps/zephyr-hello-shell}"
BUILD_DIR="${ZEPHYR_BUILD_DIR:-/tmp/stm32h7-zephyr-hello-shell}"
BOARD="${ZEPHYR_BOARD:-nucleo_h755zi_q/stm32h755xx/m7}"
WEST_BIN="${WEST_BIN:-west}"
PROGRAMMER="${STM32_PROGRAMMER_CLI:-}"
SWD_FREQ="${STM32_PROGRAMMER_SWD_FREQ:-8000}"
PORT="${ZEPHYR_UART_PORT:-}"
VALIDATION_RETRIES="${ZEPHYR_UART_VALIDATION_RETRIES:-3}"
ZEPHYR_SDK_ROOT="${ZEPHYR_SDK_ROOT:-}"
HOST_IPV4_ADDR="${ZEPHYR_HOST_IPV4_ADDR:-192.168.55.100}"
BOARD_IPV4_ADDR="${ZEPHYR_BOARD_IPV4_ADDR:-192.168.55.2}"
BOARD_IPV4_NETMASK="${ZEPHYR_BOARD_IPV4_NETMASK:-255.255.255.0}"

DO_BUILD=1
DO_FLASH=1
DO_VALIDATE=1

usage() {
  cat <<'EOF'
Usage: scripts/flash_zephyr_hello_shell.sh [options]

Build, flash, and validate the external Zephyr hello-shell app.

Options:
  --build-only          Rebuild only.
  --flash-only          Flash only.
  --validate-only       Validate UART, Ethernet, and HTTP behavior only.
  --skip-validate       Build and/or flash without runtime validation.
  --build-dir PATH      Override the Zephyr build directory.
  --app-dir PATH        Override the external Zephyr app directory.
  --zephyr-base PATH    Override the Zephyr base directory.
  --board TARGET        Override the Zephyr board target.
  --port PATH           Override the UART device path.
  --programmer PATH     Override the STM32_Programmer_CLI path.
  --freq KHZ            Override the SWD frequency in kHz.
  --host-ipv4 ADDR      Override the host IPv4 address used for Ethernet validation.
  --board-ipv4 ADDR     Override the board static IPv4 address expected by validation.
  --board-netmask MASK  Override the board IPv4 netmask expected by validation.
  -h, --help            Show this help text.

Environment overrides:
  ZEPHYR_BASE
  ZEPHYR_APP_DIR
  ZEPHYR_BUILD_DIR
  ZEPHYR_BOARD
  ZEPHYR_UART_PORT
  ZEPHYR_UART_VALIDATION_RETRIES
  ZEPHYR_SDK_ROOT
  ZEPHYR_HOST_IPV4_ADDR
  ZEPHYR_BOARD_IPV4_ADDR
  ZEPHYR_BOARD_IPV4_NETMASK
  STM32_PROGRAMMER_CLI
  STM32_PROGRAMMER_SWD_FREQ
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
    --build-only)
      DO_BUILD=1
      DO_FLASH=0
      DO_VALIDATE=0
      ;;
    --flash-only)
      DO_BUILD=0
      DO_FLASH=1
      ;;
    --validate-only)
      DO_BUILD=0
      DO_FLASH=0
      DO_VALIDATE=1
      ;;
    --skip-validate)
      DO_VALIDATE=0
      ;;
    --build-dir)
      shift
      [[ $# -gt 0 ]] || die "--build-dir requires a value"
      BUILD_DIR="$1"
      ;;
    --app-dir)
      shift
      [[ $# -gt 0 ]] || die "--app-dir requires a value"
      APP_DIR="$1"
      ;;
    --zephyr-base)
      shift
      [[ $# -gt 0 ]] || die "--zephyr-base requires a value"
      ZEPHYR_BASE="$1"
      ;;
    --board)
      shift
      [[ $# -gt 0 ]] || die "--board requires a value"
      BOARD="$1"
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
    --host-ipv4)
      shift
      [[ $# -gt 0 ]] || die "--host-ipv4 requires a value"
      HOST_IPV4_ADDR="$1"
      ;;
    --board-ipv4)
      shift
      [[ $# -gt 0 ]] || die "--board-ipv4 requires a value"
      BOARD_IPV4_ADDR="$1"
      ;;
    --board-netmask)
      shift
      [[ $# -gt 0 ]] || die "--board-netmask requires a value"
      BOARD_IPV4_NETMASK="$1"
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

[[ -d "${ZEPHYR_BASE}" ]] || die "Zephyr base not found: ${ZEPHYR_BASE}"
[[ -d "${APP_DIR}" ]] || die "Zephyr app not found: ${APP_DIR}"
if [[ "${DO_FLASH}" -eq 1 || "${DO_VALIDATE}" -eq 1 ]]; then
  if [[ -z "${PROGRAMMER}" ]]; then
    for candidate in /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin/STM32_Programmer_CLI; do
      if [[ -x "${candidate}" ]]; then
        PROGRAMMER="${candidate}"
        break
      fi
    done
  fi
  [[ -n "${PROGRAMMER}" && -x "${PROGRAMMER}" ]] || die "STM32_Programmer_CLI not found; set STM32_PROGRAMMER_CLI (found: ${PROGRAMMER:-none})"
fi
command -v "${WEST_BIN}" >/dev/null 2>&1 || die "west not found: ${WEST_BIN}"
command -v python3 >/dev/null 2>&1 || die "python3 not found"

prepare_toolchain() {
  if [[ -n "${ZEPHYR_TOOLCHAIN_VARIANT:-}" || -n "${CROSS_COMPILE:-}" ]]; then
    return
  fi

  if [[ -z "${ZEPHYR_SDK_ROOT}" ]]; then
    local candidate=""
    for candidate in /home/"${USER}"/.local/zephyr-sdk-* "${HOME}/.local/zephyr-sdk-"*; do
      if [[ -d "${candidate}" && -x "${candidate}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]; then
        ZEPHYR_SDK_ROOT="${candidate}"
        break
      fi
    done
  fi

  [[ -n "${ZEPHYR_SDK_ROOT}" ]] || die "Zephyr SDK not found; set ZEPHYR_SDK_ROOT or export CROSS_COMPILE/ZEPHYR_TOOLCHAIN_VARIANT"
  [[ -x "${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]] || die "Zephyr SDK toolchain missing under ${ZEPHYR_SDK_ROOT}"

  export ZEPHYR_TOOLCHAIN_VARIANT="cross-compile"
  export CROSS_COMPILE="${ZEPHYR_SDK_ROOT}/arm-zephyr-eabi/bin/arm-zephyr-eabi-"
}

build_app() {
  prepare_toolchain
  local toolchain_args=()
  # Cross-compile does not detect the standalone SDK's full C/C++ libc.
  if [[ "${ZEPHYR_TOOLCHAIN_VARIANT:-}" == "cross-compile" ]]; then
    local picolibc_specs
    picolibc_specs="$("${CROSS_COMPILE}gcc" -print-file-name=picolibc.specs)"
    if [[ -f "${picolibc_specs}" ]]; then
      toolchain_args+=("-DTOOLCHAIN_HAS_PICOLIBC=ON")
      toolchain_args+=("-DCONFIG_TOOLCHAIN_CROSS_COMPILE_SUPPORTS_THREAD_LOCAL_STORAGE=y")
    fi
  fi
  log "building ${APP_DIR} for ${BOARD}"
  (
    cd "${ZEPHYR_BASE}"
    "${WEST_BIN}" build -p always -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}" -- "${toolchain_args[@]}"
  )
}

flash_app() {
  [[ -f "${BUILD_DIR}/zephyr/zephyr.hex" ]] || die "missing build artifact: ${BUILD_DIR}/zephyr/zephyr.hex"

  local option_output bcm4_lines bcm4_count bcm4_line bcm4_token bcm4_value
  log "reading BCM4 option byte"
  if ! option_output="$("${PROGRAMMER}" -c "port=SWD" "freq=${SWD_FREQ}" -ob displ 2>&1)"; then
    printf '%s\n' "${option_output}" >&2
    die "failed to read option bytes from STM32 target"
  fi
  bcm4_lines="$(printf '%s\n' "${option_output}" | grep -E '^[[:space:]]*BCM4[[:space:]]*:' || true)"
  bcm4_count="$(printf '%s\n' "${bcm4_lines}" | sed '/^$/d' | wc -l | tr -d '[:space:]')"
  if [[ "${bcm4_count}" != "1" ]]; then
    printf '%s\n' "${option_output}" >&2
    die "unable to parse BCM4 option byte from STM32_Programmer_CLI output"
  fi
  bcm4_line="$(printf '%s\n' "${bcm4_lines}" | head -n 1)"
  bcm4_token="$(printf '%s\n' "${bcm4_line}" | cut -d: -f2- | xargs | awk '{print $1}')"
  bcm4_value="${bcm4_token,,}"
  case "${bcm4_value}" in
    0|0x0)
      ;;
    1|0x1)
      die "BCM4 option byte must be 0 before flashing bank2 sector0 firmware"
      ;;
    *)
      printf '%s\n' "${option_output}" >&2
      die "unable to parse BCM4 option byte from STM32_Programmer_CLI output"
      ;;
  esac

  log "flashing ${BUILD_DIR}"
  (
    cd "${ZEPHYR_BASE}"
    PATH="$(dirname -- "${PROGRAMMER}"):${PATH}" "${WEST_BIN}" flash -d "${BUILD_DIR}" --runner stm32cubeprogrammer
  )
}

validate_uart() {
  detect_port
  log "validating UART on ${PORT}"

  ZEPHYR_UART_PORT="${PORT}" \
  STM32_PROGRAMMER_CLI="${PROGRAMMER}" \
  STM32_PROGRAMMER_SWD_FREQ="${SWD_FREQ}" \
  ZEPHYR_UART_VALIDATION_RETRIES="${VALIDATION_RETRIES}" \
  ZEPHYR_BASE="${ZEPHYR_BASE}" \
  ZEPHYR_BUILD_DIR="${BUILD_DIR}" \
  WEST_BIN="${WEST_BIN}" \
  ZEPHYR_BOARD_TARGET="${BOARD}" \
  ZEPHYR_HOST_IPV4_ADDR="${HOST_IPV4_ADDR}" \
  ZEPHYR_BOARD_IPV4_ADDR="${BOARD_IPV4_ADDR}" \
  ZEPHYR_BOARD_IPV4_NETMASK="${BOARD_IPV4_NETMASK}" \
  FLASH_BEFORE_VALIDATE="${FLASH_BEFORE_VALIDATE:-0}" \
  python3 - <<'PY'
import json
import os
import re
import subprocess
import sys
import time

try:
    import serial
except Exception as exc:
    print(f"error: pyserial is required for UART validation: {exc}", file=sys.stderr)
    sys.exit(2)

PORT = os.environ["ZEPHYR_UART_PORT"]
PROGRAMMER = os.environ["STM32_PROGRAMMER_CLI"]
SWD_FREQ = os.environ["STM32_PROGRAMMER_SWD_FREQ"]
RETRIES = int(os.environ["ZEPHYR_UART_VALIDATION_RETRIES"])
ZEPHYR_BASE = os.environ["ZEPHYR_BASE"]
BUILD_DIR = os.environ["ZEPHYR_BUILD_DIR"]
WEST_BIN = os.environ["WEST_BIN"]
BOARD = os.environ["ZEPHYR_BOARD_TARGET"]
FLASH_BEFORE_VALIDATE = os.environ.get("FLASH_BEFORE_VALIDATE") == "1"
HOST_IPV4_ADDR = os.environ["ZEPHYR_HOST_IPV4_ADDR"]
BOARD_IPV4_ADDR = os.environ["ZEPHYR_BOARD_IPV4_ADDR"]
BOARD_IPV4_NETMASK = os.environ["ZEPHYR_BOARD_IPV4_NETMASK"]

def run_programmer(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        [PROGRAMMER, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=60,
        check=False,
    )
    if check and proc.returncode != 0:
        print(proc.stdout, end="")
        raise SystemExit(proc.returncode)
    return proc

def run_host_ping(address: str, attempts: int = 3, delay_sec: float = 1.0) -> tuple[bool, str]:
    last_output = ""
    for attempt in range(1, attempts + 1):
        try:
            proc = subprocess.run(
                ["ping", "-c", "1", "-W", "2", address],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=10,
                check=False,
            )
        except FileNotFoundError as exc:
            raise RuntimeError(f"ping command not found: {exc}") from exc

        last_output = proc.stdout
        if proc.returncode == 0:
            return True, last_output

        if attempt < attempts:
            time.sleep(delay_sec)

    return False, last_output

def run_host_curl(args: list[str], attempts: int = 3, delay_sec: float = 1.0) -> tuple[bool, str]:
    last_output = ""
    for attempt in range(1, attempts + 1):
        try:
            proc = subprocess.run(
                ["curl", "--silent", "--show-error", "--max-time", "5", *args],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=10,
                check=False,
            )
        except FileNotFoundError as exc:
            raise RuntimeError(f"curl command not found: {exc}") from exc

        last_output = proc.stdout
        if proc.returncode == 0:
            return True, last_output

        if attempt < attempts:
            time.sleep(delay_sec)

    return False, last_output

def run_host_curl_sequence(
    args: list[str], count: int, delay_sec: float = 0.5
) -> tuple[bool, str, list[object | None]]:
    outputs: list[str] = []
    documents: list[object | None] = []

    for index in range(count):
        ok, output = run_host_curl(args, attempts=1, delay_sec=delay_sec)
        outputs.append(f"sample {index + 1}: {output.strip()}")
        if not ok:
            return False, "\n".join(outputs), documents

        documents.append(parse_json_document(output))

        if index + 1 < count:
            time.sleep(delay_sec)

    return True, "\n".join(outputs), documents

def parse_json_document(raw: str) -> object | None:
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None

def extract_led_state(document: object, color: str) -> str | None:
    if not isinstance(document, dict):
        return None

    if isinstance(document.get("io"), dict):
        document = document["io"]

    leds = document.get("leds")
    if not isinstance(leds, list):
        return None

    for led in leds:
        if isinstance(led, dict) and led.get("color") == color:
            state = led.get("state")
            if isinstance(state, str):
                return state

    return None

def has_mouse_snapshot(io: object) -> bool:
    if not isinstance(io, dict):
        return False
    mouse = io.get("mouse")
    if not isinstance(mouse, dict):
        return False
    presses = mouse.get("presses")
    return (
        isinstance(mouse.get("connected"), bool)
        and all(isinstance(mouse.get(field), int) for field in
                ("session", "reports", "buttons", "x", "y", "wheel"))
        and isinstance(presses, dict)
        and all(isinstance(presses.get(button), int) for button in
                ("left", "right", "middle"))
    )

def flash_build() -> None:
    env = os.environ.copy()
    env["PATH"] = f"{os.path.dirname(PROGRAMMER)}:{env['PATH']}"
    proc = subprocess.run(
        [WEST_BIN, "flash", "-d", BUILD_DIR, "--runner", "stm32cubeprogrammer"],
        cwd=ZEPHYR_BASE,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=180,
        check=False,
    )
    print(proc.stdout, end="")
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)

def open_serial(timeout_sec: float = 10.0) -> serial.Serial:
    deadline = time.time() + timeout_sec
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            ser = serial.Serial(PORT, 115200, timeout=0.25, write_timeout=1)
            try:
                ser.dtr = True
                ser.rts = True
            except Exception:
                pass
            return ser
        except Exception as exc:
            last_error = exc
            time.sleep(0.2)
    raise RuntimeError(f"failed to open {PORT}: {last_error}")

def reopen_serial(ser: serial.Serial | None, delay_sec: float = 0.5) -> serial.Serial:
    if ser is not None:
        try:
            ser.close()
        except Exception:
            pass
    time.sleep(delay_sec)
    return open_serial()

def read_until(ser: serial.Serial, timeout_sec: float, patterns: tuple[str, ...] = ()) -> str:
    deadline = time.time() + timeout_sec
    chunks: list[str] = []
    while time.time() < deadline:
        try:
            waiting = ser.in_waiting
            if waiting:
                data = ser.read(waiting)
            else:
                data = ser.read(256)
        except serial.SerialException:
            raise
        if data:
            text = data.decode("utf-8", "replace")
            chunks.append(text)
            transcript = "".join(chunks)
            if patterns and all(pattern in transcript for pattern in patterns):
                return transcript
        else:
            time.sleep(0.05)
    return "".join(chunks)

def read_until_quiet(ser: serial.Serial, quiet_sec: float = 1.5, max_sec: float = 10.0) -> str:
    """Drain boot-time logs (e.g. USB host enumeration) so they don't interleave with shell checks."""
    deadline = time.time() + max_sec
    last_data = time.time()
    chunks: list[str] = []
    while time.time() < deadline and time.time() - last_data < quiet_sec:
        data = ser.read(ser.in_waiting or 1)
        if data:
            chunks.append(data.decode("utf-8", "replace"))
            last_data = time.time()
    return "".join(chunks)

def run_shell_command(ser: serial.Serial, command: str, timeout_sec: float = 6.0) -> str:
    ser.reset_input_buffer()
    payload = "\n" if not command else f"{command}\n"
    ser.write(payload.encode())
    ser.flush()
    deadline = time.time() + timeout_sec
    chunks: list[str] = []
    while time.time() < deadline:
        try:
            waiting = ser.in_waiting
            if waiting:
                data = ser.read(waiting)
            else:
                data = ser.read(256)
        except serial.SerialException:
            time.sleep(0.2)
            continue
        if data:
            text = data.decode("utf-8", "replace")
            chunks.append(text)
            if "uart:~$" in "".join(chunks):
                break
        else:
            time.sleep(0.05)
    return "".join(chunks)

def wait_for_shell_predicate(
    ser: serial.Serial,
    command: str,
    predicate,
    attempts: int = 6,
    delay_sec: float = 1.0,
    timeout_sec: float = 6.0,
) -> tuple[serial.Serial, str]:
    last_output = ""
    for attempt in range(1, attempts + 1):
        ser, output = run_shell_command_resilient(ser, command, timeout_sec=timeout_sec)
        last_output = output
        if predicate(output):
            return ser, output

        if attempt < attempts:
            time.sleep(delay_sec)

    return ser, last_output

def parse_led_state(output: str, color: str) -> str | None:
    match = re.search(rf"{re.escape(color)}:\s+(on|off)", output)
    if match is None:
        return None
    return match.group(1)

def sample_led_states(
    ser: serial.Serial,
    color: str,
    samples: int,
    delay_sec: float,
) -> tuple[serial.Serial, list[str]]:
    states: list[str] = []
    for index in range(samples):
        ser, output = run_shell_command_resilient(ser, f"leds status {color}")
        print(f"--- leds status {color} sample {index + 1} ---")
        print(output.strip())
        state = parse_led_state(output, color)
        if state is not None:
            states.append(state)
        if index + 1 < samples:
            time.sleep(delay_sec)
    return ser, states

def read_until_resilient(
    ser: serial.Serial,
    timeout_sec: float,
    patterns: tuple[str, ...] = (),
    reopen_attempts: int = 3,
) -> tuple[serial.Serial, str]:
    chunks: list[str] = []
    for attempt in range(1, reopen_attempts + 1):
        try:
            chunks.append(read_until(ser, timeout_sec, patterns=patterns))
            return ser, "".join(chunks)
        except serial.SerialException as exc:
            print(f"serial read attempt {attempt} failed: {exc}")
            if attempt == reopen_attempts:
                raise
            ser = reopen_serial(ser)
    return ser, "".join(chunks)

def run_shell_command_resilient(
    ser: serial.Serial,
    command: str,
    timeout_sec: float = 6.0,
    reopen_attempts: int = 3,
) -> tuple[serial.Serial, str]:
    for attempt in range(1, reopen_attempts + 1):
        try:
            return ser, run_shell_command(ser, command, timeout_sec=timeout_sec)
        except serial.SerialException as exc:
            print(f"serial shell attempt {attempt} for {command!r} failed: {exc}")
            if attempt == reopen_attempts:
                raise
            ser = reopen_serial(ser)
    return ser, ""

def attempt_validation(attempt: int) -> bool:
    print(f"==> UART validation attempt {attempt}")
    ser = None
    try:
        ser = open_serial()
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        if FLASH_BEFORE_VALIDATE:
            flash_build()
            time.sleep(1.0)

        actions = [
            ("passive", None),
            ("hard-reset", ("-c", "port=SWD", "mode=UR", "reset=HWrst", f"freq={SWD_FREQ}", "-hardRst")),
        ]

        transcript = ""
        for name, programmer_args in actions:
            if programmer_args is not None:
                if name == "hard-reset":
                    try:
                        if ser is not None:
                            ser.close()
                    except Exception:
                        pass
                    ser = None
                    proc = run_programmer(*programmer_args, check=False)
                    if proc.stdout.strip():
                        print(proc.stdout.strip())
                    ser = open_serial(timeout_sec=15.0)
                    time.sleep(0.5)
                else:
                    proc = run_programmer(*programmer_args, check=False)
                    if proc.stdout.strip():
                        print(proc.stdout.strip())

            ser, chunk = read_until_resilient(ser, 8.0, patterns=("Hello World!", "uart:~$"))
            transcript += chunk

            if "Hello World!" in transcript and "uart:~$" in transcript:
                break

        transcript += read_until_quiet(ser)

        print("--- transcript ---")
        print(transcript.strip())

        ser, prompt_output = run_shell_command_resilient(ser, "")
        ser, help_output = run_shell_command_resilient(ser, "help")
        ser, version_output = run_shell_command_resilient(ser, "kernel version")
        ser, led_on_output = run_shell_command_resilient(ser, "leds on green")
        ser, led_status_output = run_shell_command_resilient(ser, "leds status green")
        ser, led_off_output = run_shell_command_resilient(ser, "leds off all")
        ser, yellow_states = sample_led_states(ser, "yellow", samples=6, delay_sec=0.3)
        ser, red_states = sample_led_states(ser, "red", samples=8, delay_sec=0.3)
        ser, net_iface_output = wait_for_shell_predicate(
            ser,
            "net iface",
            lambda output: (
                "Ethernet" in output
                and BOARD_IPV4_ADDR in output
                and BOARD_IPV4_NETMASK in output
            ),
            attempts=8,
            delay_sec=1.0,
            timeout_sec=8.0,
        )
        ser, net_ping_output = wait_for_shell_predicate(
            ser,
            f"net ping -c 1 {HOST_IPV4_ADDR}",
            lambda output: f"bytes from {HOST_IPV4_ADDR}" in output,
            attempts=3,
            delay_sec=1.0,
            timeout_sec=10.0,
        )
        host_ping_ok, host_ping_output = run_host_ping(BOARD_IPV4_ADDR)
        host_http_root_ok, host_http_root_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/"],
            attempts=6,
            delay_sec=1.0,
        )
        host_http_status_ok, host_http_status_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/status"],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_status_repeat_ok, host_http_status_repeat_output, host_http_status_repeat_docs = run_host_curl_sequence(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/status"],
            count=5,
            delay_sec=0.5,
        )
        host_http_io_ok, host_http_io_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/io"],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_mouse_ok, host_http_mouse_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/mouse"],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_led_on_ok, host_http_led_on_output = run_host_curl(
            [
                "--fail",
                "-X",
                "POST",
                "-H",
                "Content-Type: application/json",
                "--data",
                '{"led":"green","action":"on"}',
                f"http://{BOARD_IPV4_ADDR}/api/v1/leds",
            ],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_io_after_on_ok, host_http_io_after_on_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/io"],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_led_off_ok, host_http_led_off_output = run_host_curl(
            [
                "--fail",
                "-X",
                "POST",
                "-H",
                "Content-Type: application/json",
                "--data",
                '{"led":"green","action":"off"}',
                f"http://{BOARD_IPV4_ADDR}/api/v1/leds",
            ],
            attempts=3,
            delay_sec=1.0,
        )
        host_http_io_after_off_ok, host_http_io_after_off_output = run_host_curl(
            ["--fail", f"http://{BOARD_IPV4_ADDR}/api/v1/io"],
            attempts=3,
            delay_sec=1.0,
        )

        host_http_status_doc = parse_json_document(host_http_status_output)
        host_http_io_doc = parse_json_document(host_http_io_output)
        host_http_mouse_doc = parse_json_document(host_http_mouse_output)
        host_http_led_on_doc = parse_json_document(host_http_led_on_output)
        host_http_io_after_on_doc = parse_json_document(host_http_io_after_on_output)
        host_http_led_off_doc = parse_json_document(host_http_led_off_output)
        host_http_io_after_off_doc = parse_json_document(host_http_io_after_off_output)

        print("--- prompt ---")
        print(prompt_output.strip())
        print("--- help ---")
        print(help_output.strip())
        print("--- kernel version ---")
        print(version_output.strip())
        print("--- leds on green ---")
        print(led_on_output.strip())
        print("--- leds status green ---")
        print(led_status_output.strip())
        print("--- leds off all ---")
        print(led_off_output.strip())
        print("--- yellow blink states ---")
        print(yellow_states)
        print("--- red blink states ---")
        print(red_states)
        print("--- net iface ---")
        print(net_iface_output.strip())
        print(f"--- net ping {HOST_IPV4_ADDR} ---")
        print(net_ping_output.strip())
        print(f"--- host ping {BOARD_IPV4_ADDR} ---")
        print(host_ping_output.strip())
        print(f"--- host http GET / ---")
        print(host_http_root_output.strip())
        print(f"--- host http GET /api/v1/status ---")
        print(host_http_status_output.strip())
        print(f"--- host http GET /api/v1/status repeated ---")
        print(host_http_status_repeat_output.strip())
        print(f"--- host http GET /api/v1/io ---")
        print(host_http_io_output.strip())
        print(f"--- host http GET /api/v1/mouse ---")
        print(host_http_mouse_output.strip())
        print(f"--- host http POST /api/v1/leds on ---")
        print(host_http_led_on_output.strip())
        print(f"--- host http GET /api/v1/io after on ---")
        print(host_http_io_after_on_output.strip())
        print(f"--- host http POST /api/v1/leds off ---")
        print(host_http_led_off_output.strip())
        print(f"--- host http GET /api/v1/io after off ---")
        print(host_http_io_after_off_output.strip())

        hello_ok = "Hello World!" in transcript
        blink_start_ok = "Blink threads started for yellow" in transcript
        http_start_ok = "HTTP server listening on http://" in transcript
        prompt_ok = "uart:~$" in transcript or "uart:~$" in prompt_output
        help_ok = "Available commands:" in help_output
        version_ok = re.search(r"Zephyr version\s+\S+", version_output) is not None
        led_on_ok = "green -> on" in led_on_output
        led_status_ok = "green: on" in led_status_output
        led_off_ok = all(token in led_off_output for token in ("green -> off", "yellow -> off", "red -> off"))
        yellow_blink_ok = len(set(yellow_states)) > 1
        red_blink_ok = len(set(red_states)) > 1
        net_iface_ok = (
            "Ethernet" in net_iface_output
            and BOARD_IPV4_ADDR in net_iface_output
            and BOARD_IPV4_NETMASK in net_iface_output
        )
        net_ping_ok = f"bytes from {HOST_IPV4_ADDR}" in net_ping_output
        host_ping_ok = host_ping_ok
        host_http_root_ok = host_http_root_ok and "<title>Embedded device dashboard</title>" in host_http_root_output
        host_http_status_ok = (
            host_http_status_ok
            and isinstance(host_http_status_doc, dict)
            and host_http_status_doc.get("board") == BOARD
            and isinstance(host_http_status_doc.get("http"), dict)
            and host_http_status_doc["http"].get("port") == 80
            and isinstance(host_http_status_doc.get("network"), dict)
            and host_http_status_doc["network"].get("ipv4") == BOARD_IPV4_ADDR
            and has_mouse_snapshot(host_http_status_doc.get("io"))
        )
        repeated_uptimes = [
            document.get("uptime_ms")
            for document in host_http_status_repeat_docs
            if isinstance(document, dict)
        ]
        host_http_status_repeat_ok = (
            host_http_status_repeat_ok
            and len(host_http_status_repeat_docs) == 5
            and all(isinstance(document, dict) for document in host_http_status_repeat_docs)
            and all(document.get("board") == BOARD for document in host_http_status_repeat_docs if isinstance(document, dict))
            and all(
                isinstance(document.get("network"), dict)
                and document["network"].get("ipv4") == BOARD_IPV4_ADDR
                for document in host_http_status_repeat_docs
                if isinstance(document, dict)
            )
            and all(
                extract_led_state(document, "yellow") in {"on", "off"}
                and extract_led_state(document, "red") in {"on", "off"}
                for document in host_http_status_repeat_docs
            )
            and len(repeated_uptimes) == 5
            and all(isinstance(uptime, int) for uptime in repeated_uptimes)
            and all(
                repeated_uptimes[index] <= repeated_uptimes[index + 1]
                for index in range(len(repeated_uptimes) - 1)
            )
        )
        host_http_io_ok = (
            host_http_io_ok
            and isinstance(host_http_io_doc, dict)
            and isinstance(host_http_io_doc.get("button"), dict)
            and extract_led_state(host_http_io_doc, "green") in {"on", "off"}
            and has_mouse_snapshot(host_http_io_doc)
        )
        host_http_mouse_ok = host_http_mouse_ok and has_mouse_snapshot(
            {"mouse": host_http_mouse_doc}
        )
        host_http_led_on_ok = (
            host_http_led_on_ok
            and isinstance(host_http_led_on_doc, dict)
            and host_http_led_on_doc.get("action") == "on"
            and extract_led_state(host_http_led_on_doc, "green") == "on"
        )
        host_http_io_after_on_ok = (
            host_http_io_after_on_ok
            and isinstance(host_http_io_after_on_doc, dict)
            and extract_led_state(host_http_io_after_on_doc, "green") == "on"
        )
        host_http_led_off_ok = (
            host_http_led_off_ok
            and isinstance(host_http_led_off_doc, dict)
            and host_http_led_off_doc.get("action") == "off"
            and extract_led_state(host_http_led_off_doc, "green") == "off"
        )
        host_http_io_after_off_ok = (
            host_http_io_after_off_ok
            and isinstance(host_http_io_after_off_doc, dict)
            and extract_led_state(host_http_io_after_off_doc, "green") == "off"
        )

        print("--- summary ---")
        print(f"hello_ok={hello_ok}")
        print(f"blink_start_ok={blink_start_ok}")
        print(f"http_start_ok={http_start_ok}")
        print(f"prompt_ok={prompt_ok}")
        print(f"help_ok={help_ok}")
        print(f"version_ok={version_ok}")
        print(f"led_on_ok={led_on_ok}")
        print(f"led_status_ok={led_status_ok}")
        print(f"led_off_ok={led_off_ok}")
        print(f"yellow_blink_ok={yellow_blink_ok}")
        print(f"red_blink_ok={red_blink_ok}")
        print(f"net_iface_ok={net_iface_ok}")
        print(f"net_ping_ok={net_ping_ok}")
        print(f"host_ping_ok={host_ping_ok}")
        print(f"host_http_root_ok={host_http_root_ok}")
        print(f"host_http_status_ok={host_http_status_ok}")
        print(f"host_http_status_repeat_ok={host_http_status_repeat_ok}")
        print(f"host_http_io_ok={host_http_io_ok}")
        print(f"host_http_mouse_ok={host_http_mouse_ok}")
        print(f"host_http_led_on_ok={host_http_led_on_ok}")
        print(f"host_http_io_after_on_ok={host_http_io_after_on_ok}")
        print(f"host_http_led_off_ok={host_http_led_off_ok}")
        print(f"host_http_io_after_off_ok={host_http_io_after_off_ok}")
        return (
            hello_ok
            and blink_start_ok
            and http_start_ok
            and prompt_ok
            and help_ok
            and version_ok
            and led_on_ok
            and led_status_ok
            and led_off_ok
            and yellow_blink_ok
            and red_blink_ok
            and net_iface_ok
            and net_ping_ok
            and host_ping_ok
            and host_http_root_ok
            and host_http_status_ok
            and host_http_status_repeat_ok
            and host_http_io_ok
            and host_http_mouse_ok
            and host_http_led_on_ok
            and host_http_io_after_on_ok
            and host_http_led_off_ok
            and host_http_io_after_off_ok
        )
    except Exception as exc:
        print(f"validation attempt {attempt} failed: {exc}")
        return False
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass

for attempt in range(1, RETRIES + 1):
    if attempt_validation(attempt):
        sys.exit(0)

print("error: validation did not observe the expected boot, shell, ethernet, and HTTP responses", file=sys.stderr)
sys.exit(3)
PY
}

if [[ "${DO_BUILD}" -eq 1 ]]; then
  build_app
fi

if [[ "${DO_FLASH}" -eq 1 && "${DO_VALIDATE}" -eq 1 ]]; then
  FLASH_BEFORE_VALIDATE=1 validate_uart
elif [[ "${DO_FLASH}" -eq 1 ]]; then
  flash_app
fi

if [[ "${DO_VALIDATE}" -eq 1 && ! ( "${DO_FLASH}" -eq 1 ) ]]; then
  validate_uart
fi
