#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR_BASE="${ZEPHYR_BASE:-${REPO_ROOT}/vendor/zephyrproject/zephyr}"
WEST_BIN="${WEST_BIN:-west}"
BOARD_TARGET="${ZEPHYR_SAMPLE_BOARD:-nucleo_h755zi_q/stm32h755xx/m7}"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
Usage: bash scripts/flash_zephyr_sample.sh <sample> [--build-only|--flash-only]
                                           [--pristine] [-- <extra west/cmake args>]
       bash scripts/flash_zephyr_sample.sh --list [filter]

Build and flash any upstream Zephyr sample (under $ZEPHYR_BASE/samples) to the
NUCLEO-H755ZI-Q Cortex-M7 core. <sample> is a path relative to samples/
(basic/blinky), a path starting with samples/, or a unique short name (blinky).
No application validation is done; watch the UART console yourself.

Build dir: build/sample-<name> (override with ZEPHYR_SAMPLE_BUILD_DIR).
Overrides: ZEPHYR_BASE, ZEPHYR_VENV (default ~/.venvs/zephyr-4.4), ZEPHYR_SDK_ROOT, WEST_BIN, ZEPHYR_SAMPLE_BOARD,
STM32_PROGRAMMER_CLI.
EOF
}

list_samples() {
  local filter="${1:-}"
  [[ -d "${ZEPHYR_BASE}/samples" ]] || die "Zephyr samples not found: ${ZEPHYR_BASE}/samples"
  (cd "${ZEPHYR_BASE}/samples" && find . -name CMakeLists.txt -printf '%h\n' | sed 's|^\./||' | sort) |
    { if [[ -n "${filter}" ]]; then grep -F -- "${filter}" || true; else cat; fi; }
}

# Prints the sample path relative to $ZEPHYR_BASE; sets exit 1 on no/ambiguous match.
resolve_sample() {
  local spec="${1#samples/}" matches=()
  spec="${spec%/}"
  if [[ -f "${ZEPHYR_BASE}/samples/${spec}/CMakeLists.txt" ]]; then
    printf 'samples/%s\n' "${spec}"
    return 0
  fi
  mapfile -t matches < <(list_samples | awk -F/ -v n="${spec}" '$NF == n')
  if [[ ${#matches[@]} -eq 1 ]]; then
    printf 'samples/%s\n' "${matches[0]}"
    return 0
  fi
  if [[ ${#matches[@]} -gt 1 ]]; then
    printf 'error: ambiguous sample "%s"; use one of:\n' "${spec}" >&2
    printf '  %s\n' "${matches[@]}" >&2
  else
    printf 'error: sample not found: %s (try --list)\n' "${spec}" >&2
  fi
  return 1
}

case "${1:-}" in
  ""|-h|--help) usage; [[ -n "${1:-}" ]] && exit 0 || exit 2 ;;
  --list) shift; list_samples "${1:-}"; exit 0 ;;
esac

[[ -d "${ZEPHYR_BASE}" ]] || die "Zephyr base not found: ${ZEPHYR_BASE}"
# Zephyr 4.4 needs Python 3.12+; use the documented venv if not already active.
ZEPHYR_VENV="${ZEPHYR_VENV:-${HOME}/.venvs/zephyr-4.4}"
if [[ -z "${VIRTUAL_ENV:-}" && -f "${ZEPHYR_VENV}/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "${ZEPHYR_VENV}/bin/activate"
fi
sample="$(resolve_sample "$1")" || exit 1
shift

do_build=1 do_flash=1 pristine=()
extra=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-only) do_flash=0 ;;
    --flash-only) do_build=0 ;;
    --pristine) pristine=(-p always) ;;
    --) shift; extra=("$@"); break ;;
    *) die "unknown option: $1; use --help" ;;
  esac
  shift
done
[[ ${do_build} -eq 1 || ${do_flash} -eq 1 ]] || die "--build-only and --flash-only conflict"

name="${sample#samples/}"
BUILD_DIR="${ZEPHYR_SAMPLE_BUILD_DIR:-${REPO_ROOT}/build/sample-${name//\//-}}"

if [[ ${do_build} -eq 1 ]]; then
  SDK="${ZEPHYR_SDK_ROOT:-}"
  if [[ -z "${SDK}" ]]; then
    for candidate in "${HOME}"/.local/zephyr-sdk-*; do
      if [[ -x "${candidate}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]; then
        SDK="${candidate}"
        break
      fi
    done
  fi
  [[ -x "${SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]] || die "Zephyr SDK ARM toolchain not found; set ZEPHYR_SDK_ROOT"
  (
    cd "${ZEPHYR_BASE}"
    ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
    CROSS_COMPILE="${SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-" \
      "${WEST_BIN}" build -b "${BOARD_TARGET}" "${pristine[@]}" \
      "${sample}" -d "${BUILD_DIR}" ${extra[@]+-- "${extra[@]}"}
  )
fi

if [[ ${do_flash} -eq 1 ]]; then
  [[ -s "${BUILD_DIR}/zephyr/zephyr.hex" ]] || die "HEX missing; build first: ${BUILD_DIR}/zephyr/zephyr.hex"
  "${SCRIPT_DIR}/flash_zephyr_hello_shell.sh" --flash-only --skip-validate --build-dir "${BUILD_DIR}"
fi
