# Copilot Instructions

## Build, test, and lint commands

This repository does not currently contain a checked-in build, test, or lint setup. There is no `Makefile`, `CMakeLists.txt`, `west.yml`, `pyproject.toml`, `pytest.ini`, `ruff.toml`, or similar project automation file in the repo root.

The only concrete checked-in automation is the PDF-to-Markdown conversion script:

- Convert all PDFs under `docs/`:
  - `python3 scripts/convert_pdfs_to_md.py docs`
- Convert a single document:
  - `python3 scripts/convert_pdfs_to_md.py docs/um3294-getting-started-with-stm32cubeh7rs-for-stm32h7sx7rx-mcus-stmicroelectronics.pdf`
- Convert a single document without OCR:
  - `python3 scripts/convert_pdfs_to_md.py --skip-ocr docs/stm32h743vi.pdf`
- Inspect the script interface:
  - `python3 scripts/convert_pdfs_to_md.py --help`

Script requirements from `scripts/convert_pdfs_to_md.py`:

- Required external tools: `pdftotext`, `pdfimages`
- Optional external tool: `tesseract` (without it, the script falls back to text extraction only)
- Required Python package: `Pillow`

## High-level architecture

This repository is currently documentation-heavy rather than a buildable firmware tree.

- `README.md` defines the repo as a playground for the STM32H7 Nucleo-144 board, focused on Zephyr bring-up, board deployment, pinmux work, boot-time optimization, driver customization, and adding devices.
- For Zephyr work on the target board in this repo, use the ST `NUCLEO-H755ZI-Q` support that Zephyr exposes as `nucleo_h755zi_q`, with dual-core targets `nucleo_h755zi_q/stm32h755xx/m7` and `nucleo_h755zi_q/stm32h755xx/m4`.
- For Zephyr builds on this board, prefer the official Zephyr SDK ARM toolchain (`arm-zephyr-eabi`) over unrelated local cross toolchains. In this repo's environment, the existing Xilinx/PetaLinux `arm-none-eabi` toolchain could build Zephyr successfully but produced an early runtime bus fault during UART init, while the Zephyr SDK toolchain booted and printed correctly on the board.
- The repo-owned Zephyr application is `apps/zephyr-hello-shell`. It now enables the on-board Ethernet MAC/PHY in addition to the UART shell and LED demo behavior.
- The validated direct host-link Ethernet setup for `apps/zephyr-hello-shell` is static IPv4 `192.168.55.2/24` on the board and `192.168.55.100/24` on the host. With link up, `net iface` should show the Ethernet interface as `oper=UP, admin=UP, carrier=ON`, and `net ping -c 1 192.168.55.100` plus host-side `ping -c 1 192.168.55.2` should both succeed.
- `scripts/flash_zephyr_hello_shell.sh` is the verified Zephyr build/flash/validation flow. Its validation now covers boot output, shell responsiveness, LED behavior, Ethernet interface state, board-to-host ping, and host-to-board ping. If the lab network differs, override the defaults with `--host-ipv4`, `--board-ipv4`, and `--board-netmask`.
- `docs/` is the main knowledge base. It keeps vendor PDFs alongside Markdown conversions of those same documents, so hardware questions should usually be answered from checked-in reference material instead of generic STM32 assumptions.
- `scripts/convert_pdfs_to_md.py` is the maintenance pipeline for `docs/`. It scans PDFs, extracts page text with Poppler tools, optionally OCRs embedded images with Tesseract, and writes sibling `.md` files with a predictable page-by-page structure.
- The documentation set spans multiple STM32H7 families. Do not assume every document matches the exact board named in `README.md`; verify the target family from the filename and document title before using it as authority.

The STM32CubeH7RS user manual in `docs/um3294-getting-started-with-stm32cubeh7rs-for-stm32h7sx7rx-mcus-stmicroelectronics.md` is the main architectural reference for future firmware layout and terminology:

- It describes the stack in STM32 terms: BSP drivers at the board/component layer, HAL for portable peripheral APIs, LL for lower-level performance-oriented APIs, and middleware/examples above them.
- It documents the expected package/project layout used by STM32Cube examples: `Applications`, `Examples`, `Examples_LL`, `Examples_MIX`, `Templates`, `Templates_LL`, and toolchain-specific folders such as `STM32CubeIDE`, `EWARM`, and `MDK-ARM`.
- It also explains the split used by XIP/LRUN templates, where projects may be divided into `Boot`, `Appli`, `Binary`, and `ExtMemloader` parts.

If future sessions add imported STM32Cube content or board code, use the HAL/LL/BSP vocabulary from that manual and preserve the upstream project structure unless there is a strong reason not to.

## Key conventions

- Treat `docs/` as source-of-truth for MCU, board, and errata details before relying on memory.
- Treat generated Markdown as convenient but imperfect. It is OCR-assisted and may contain extraction noise, so confirm important details against the document title, filenames, and nearby context before making precise claims.
- Keep generated Markdown next to its source PDF with the same basename. The script intentionally writes metadata followed by `## Page N` sections and fenced ` ```text ` blocks; preserve that format when regenerating docs.
- Prefer regenerating derived Markdown with `scripts/convert_pdfs_to_md.py` over manually editing large extracted sections.
- For pinmux and alternate-function questions, prefer `docs/rm0477-pinmux-pad-extract.md`. Despite the filename, that file explicitly says the current extract is sourced from the device datasheet `stm32h743vi.pdf`, which is the correct source for STM32H743 pin/AF mapping.
- When working from STM32Cube examples described in `um3294`, pay attention to whether an example is HAL-based, LL-based, or mixed (`Examples`, `Examples_LL`, `Examples_MIX`) and whether it builds from internal flash only or from a `Boot` + `Appli` split for external-memory flows.
- If STM32Cube-generated sources are added later, preserve the usual `USER CODE BEGIN/END` regions instead of moving user logic outside or deleting those markers.
