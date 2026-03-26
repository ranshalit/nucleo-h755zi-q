#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

IDE_ROOT="${STM32CUBEIDE_ROOT:-/opt/st/stm32cubeide_2.1.1}"
HEADLESS_BUILD="${STM32CUBEIDE_HEADLESS_BUILD:-${IDE_ROOT}/headless-build.sh}"
PROGRAMMER="${STM32_PROGRAMMER_CLI:-${IDE_ROOT}/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI}"
WORKSPACE="${STM32CUBEIDE_WORKSPACE:-}"
SWD_FREQ="${STM32_PROGRAMMER_SWD_FREQ:-8000}"
CONFIG="Debug"
DO_BUILD=1
DO_FLASH=1
WORKSPACE_EXPLICIT=0
TEMP_WORKSPACE=""

PROJECT_ROOT="${REPO_ROOT}/vendor/reference/stm32cubeh7/Projects/NUCLEO-H745ZI-Q/Examples/GPIO/GPIO_Demo/STM32CubeIDE"
CM7_PROJECT="GPIO_Demo_CM7"
CM4_PROJECT="GPIO_Demo_CM4"

usage() {
  cat <<'EOF'
Usage: scripts/flash_gpio_demo.sh [options]

Build and/or flash the verified STM32Cube GPIO_Demo reference baseline.

Options:
  --build-only         Rebuild the two images but do not flash them.
  --flash-only         Flash existing images without rebuilding.
  --config NAME        Build/flash configuration to use: Debug or Release.
  --workspace PATH     STM32CubeIDE workspace path to use for headless builds.
                       If omitted, a fresh temporary workspace is created.
  --programmer PATH    Override the STM32_Programmer_CLI path.
  --freq KHZ           SWD frequency in kHz for STM32CubeProgrammer.
  -h, --help           Show this help text.

Environment overrides:
  STM32CUBEIDE_ROOT
  STM32CUBEIDE_HEADLESS_BUILD
  STM32_PROGRAMMER_CLI
  STM32CUBEIDE_WORKSPACE
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-only)
      DO_BUILD=1
      DO_FLASH=0
      ;;
    --flash-only)
      DO_BUILD=0
      DO_FLASH=1
      ;;
    --config)
      shift
      [[ $# -gt 0 ]] || die "--config requires a value"
      CONFIG="$1"
      ;;
    --workspace)
      shift
      [[ $# -gt 0 ]] || die "--workspace requires a value"
      WORKSPACE="$1"
      WORKSPACE_EXPLICIT=1
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

case "${CONFIG}" in
  Debug|Release)
    ;;
  *)
    die "unsupported configuration '${CONFIG}', expected Debug or Release"
    ;;
esac

[[ -x "${HEADLESS_BUILD}" ]] || die "STM32CubeIDE headless build helper not found: ${HEADLESS_BUILD}"
[[ -x "${PROGRAMMER}" ]] || die "STM32_Programmer_CLI not found: ${PROGRAMMER}"
[[ -d "${PROJECT_ROOT}" ]] || die "reference project not found: ${PROJECT_ROOT}"

CM7_ELF="${PROJECT_ROOT}/CM7/${CONFIG}/${CM7_PROJECT}.elf"
CM4_ELF="${PROJECT_ROOT}/CM4/${CONFIG}/${CM4_PROJECT}.elf"

cleanup() {
  if [[ -n "${TEMP_WORKSPACE}" && -d "${TEMP_WORKSPACE}" ]]; then
    rm -rf "${TEMP_WORKSPACE}"
  fi
}

trap cleanup EXIT

prepare_workspace() {
  if [[ ${WORKSPACE_EXPLICIT} -eq 1 || -n "${WORKSPACE}" ]]; then
    mkdir -p "${WORKSPACE}"
    return
  fi

  TEMP_WORKSPACE="$(mktemp -d "${TMPDIR:-/tmp}/stm32cubeide-ws-gpio-demo.XXXXXX")"
  WORKSPACE="${TEMP_WORKSPACE}"
}

build_images() {
  prepare_workspace

  log "building ${CM7_PROJECT}/${CONFIG} and ${CM4_PROJECT}/${CONFIG}"
  log "using workspace ${WORKSPACE}"
  "${HEADLESS_BUILD}" \
    -data "${WORKSPACE}" \
    -importAll "${PROJECT_ROOT}" \
    -cleanBuild "${CM7_PROJECT}/${CONFIG}" \
    -cleanBuild "${CM4_PROJECT}/${CONFIG}" \
    -no-indexer \
    -printErrorMarkers
}

flash_images() {
  [[ -f "${CM7_ELF}" ]] || die "missing CM7 ELF: ${CM7_ELF}"
  [[ -f "${CM4_ELF}" ]] || die "missing CM4 ELF: ${CM4_ELF}"

  log "flashing ${CM7_ELF}"
  log "flashing ${CM4_ELF}"

  local -a cmd=(
    "${PROGRAMMER}"
    -c "port=SWD" "mode=UR" "reset=HWrst" "freq=${SWD_FREQ}"
    -d "${CM7_ELF}" -v
    -d "${CM4_ELF}" -v
    -hardRst
  )

  if [[ "$(id -u)" -ne 0 ]] && command -v sudo >/dev/null 2>&1; then
    sudo "${cmd[@]}"
  else
    "${cmd[@]}"
  fi

  log "board reset issued; application should now be running"
}

if [[ "${DO_BUILD}" -eq 1 ]]; then
  build_images
fi

if [[ "${DO_FLASH}" -eq 1 ]]; then
  flash_images
fi
