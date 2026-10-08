#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD="${STM32_OPENOCD:-}"
OPENOCD_SCRIPTS="${STM32_OPENOCD_SCRIPTS:-}"

CONNECT_UNDER_RESET=0

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/debug_zephyr_hello_shell.sh [--connect-under-reset]

Start an ST-LINK OpenOCD GDB server for the STM32H755 Cortex-M7 (port 3333).
Attach GDB separately with the matching Zephyr ELF; this does not flash.
By default it attaches to the running image without resetting it.

  --connect-under-reset  Hold NRST while connecting (resets the MCU). Needed
                         right after flashing with STM32CubeProgrammer, when a
                         plain attach fails with "Examination failed".

Override auto-detected STM32CubeIDE paths with STM32_OPENOCD and
STM32_OPENOCD_SCRIPTS.
EOF
  exit 0
fi

if [[ "${1:-}" == "--connect-under-reset" ]]; then
  CONNECT_UNDER_RESET=1
  shift
fi

if [[ $# -ne 0 ]]; then
  printf 'error: unexpected argument: %s (see --help)\n' "$1" >&2
  exit 1
fi

if [[ -z "${OPENOCD}" ]]; then
  for candidate in /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.openocd.linux64_*/tools/bin/openocd; do
    if [[ -x "${candidate}" ]]; then
      OPENOCD="${candidate}"
      break
    fi
  done
fi

if [[ -z "${OPENOCD}" || ! -x "${OPENOCD}" ]]; then
  printf 'error: OpenOCD not executable; set STM32_OPENOCD (found: %s)\n' "${OPENOCD:-none}" >&2
  exit 1
fi

if [[ -z "${OPENOCD_SCRIPTS}" ]]; then
  for candidate in /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.debug.openocd_*/resources/openocd/st_scripts; do
    if [[ -f "${candidate}/target/stm32h7x.cfg" ]]; then
      OPENOCD_SCRIPTS="${candidate}"
      break
    fi
  done
fi

if [[ -z "${OPENOCD_SCRIPTS}" ]]; then
  printf 'error: STM32CubeIDE OpenOCD scripts not found; set STM32_OPENOCD_SCRIPTS\n' >&2
  exit 1
fi

for config in interface/stlink-dap.cfg target/stm32h7x.cfg; do
  if [[ ! -f "$OPENOCD_SCRIPTS/$config" ]]; then
    printf 'error: OpenOCD script not found: %s\n' "$OPENOCD_SCRIPTS/$config" >&2
    exit 1
  fi
done

exec "$OPENOCD" -s "$OPENOCD_SCRIPTS" \
  -c "set CONNECT_UNDER_RESET $CONNECT_UNDER_RESET" \
  -f "$SCRIPT_DIR/stm32h755zi_cm7.openocd.cfg"
