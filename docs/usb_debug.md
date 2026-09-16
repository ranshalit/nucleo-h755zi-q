# USB Host Bring-Up Debug Log (Nucleo-H755ZI-Q, `apps/zephyr-hello-shell`)

This document is a chronological record of the investigation and fixes carried
out during USB bring-up, starting from "why doesn't the USB camera LED turn
on" and ending at the current unresolved `FHMOD=1`, `CMOD=0` forced-host
transition failure. The enumeration-only host stack is implemented but has
not yet reached enumeration on hardware. This is a debugging reference for
future USB/host work on this board, not end-user documentation.

## Starting point

A prior change added an early PD10 setup in
`apps/zephyr-hello-shell/src/boot_optimizations.c`, which drives GPIO PD10
high during early boot. This was originally described as enabling VBUS.
The exact board schematic later proved the opposite: on NUCLEO-H755ZI-Q
MB1363 D-01, U18 (`STMPS2141STR`) is active-low (`ON: 0`, `OFF: open
drain`). PD10 high safely keeps VBUS **off** until the HCD and interrupts are
ready; PD10 must then be driven low to supply VBUS to CN13.

With that change alone: board powered on, USB camera plugged into the
USER USB connector, **camera LED stayed off**.

## Step 1 — Why is the camera LED off? (root-cause analysis)

**Investigation:**
- Reviewed `git diff` of `boot_optimizations.c`: confirmed PD10 was set as
  GPIO output-high, called from both `soc_prep_hook()` and a `SYS_INIT`
  fallback. At this point its polarity was incorrectly assumed active-high;
  Step 16 corrects that assumption from the exact schematic.
- Read the initially available MB1364 manual, then later replaced it with
  the correct MB1363 UM2408/manual and D-01 schematic (Step 16). LD8 is the
  green VBUS LED; LD7 is the red overcurrent LED.
- Checked `.config`/Kconfig: `CONFIG_USB_HOST_STACK` and `CONFIG_UHC_DRIVER`
  were **not** enabled.
- Grepped the vendored Zephyr tree
  (`vendor/zephyrproject/zephyr/drivers/usb/uhc/`): only NXP MCUX, MAX3421E
  (SPI), and virtual UHC drivers exist. **No STM32 USB host-controller driver
  exists in this Zephyr checkout.**
- Grepped `subsys/usb/device_next/class/`: `usbd_uvc.c` exists but is
  **device-side** UVC only (for the board to *act as* a webcam), not a host-side
  UVC class driver that could talk to an attached camera.
- Confirmed the STM32H7 HAL *does* provide real host-mode primitives, vendored
  at
  `vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/{src,include}/stm32h7xx_hal_hcd.{c,h}`
  and `stm32h7xx_ll_usb.c` (`HAL_HCD_Init`, `HAL_HCD_Start`,
  `HAL_HCD_ResetPort`, `HAL_HCD_HC_SubmitRequest`, etc.) — but nothing in
  Zephyr wires these up automatically for host mode.

**Root cause identified:** PD10/VBUS only controls *power* to the USB
connector. Actually talking to a USB device (enumeration, reading
descriptors, video streaming) requires the SoC to act as **USB host**, which
needs a host-controller driver plus (for a camera) a host-side UVC class
driver. Neither existed in this checkout. Supplying VBUS is necessary but
not sufficient — the camera's LED lighting up depends on the host actually
enumerating it, which never happens without host-mode software.

**Result:** No code produces a working camera at this stage; this step only
established *why* — motivating the two-part fix below (Step 2 physical/power
fix, then Step 3+ actual host stack implementation).

## Step 2 — Keep PD10 high persistently (polarity corrected later)

While diagnosing, it was noticed that the early PD10 setup only runs
*once* at early boot — nothing guarantees PD10 stays driven for the life of
the application (e.g. if some other driver or pinmux re-touches PD10 later).

**Fix:** Added a `gpio-hog` devicetree node for GPIOD pin 10 in
`apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.overlay`:

```dts
&gpiod {
	usb_vbus_disable {
		gpio-hog;
		gpios = <10 GPIO_ACTIVE_HIGH>;
		output-high;
		line-name = "USB VBUS disable (active-low enable)";
	};
};
```

`CONFIG_GPIO_HOGS` keeps the pin at a fixed level independent of application
code. At the time, high was mistakenly interpreted as VBUS enabled. The exact
MB1363 schematic later proved U18 active-low: this hog is useful, but its
correct role is keeping VBUS safely **off** until HCD startup completes.

**Verification:** Built with `scripts/flash_zephyr_hello_shell.sh
--build-only`; confirmed `CONFIG_GPIO_HOGS=y` and the hog node appear in the
generated `zephyr.dts`/`.config`. Flashed with `--flash-only --skip-validate`;
STM32_Programmer_CLI reported "Application is running." After a user
power-cycle, `uart:~$ version` confirmed the new build was running.

**Result at this point:** camera LED remained off. Later evidence shows this
was because PD10 high disables U18; this step did not enable VBUS.

## Step 3 — Plan: "STM32 USB Host + UVC functionality"

Rather than attempt full UVC video streaming in one shot, the scope was
negotiated down (via the brainstorming skill) to a first milestone:
**USB host enumeration only** — detect device attach, read the device
descriptor, print vendor/product ID and class. No video streaming, no
UVC class driver, no `CONFIG_USB_HOST_STACK`/Zephyr-core UHC involvement (that
subsystem has no STM32 backend and patching Zephyr core was explicitly
out of scope).

Design doc: `docs/superpowers/specs/2026-09-16-usb-host-enumeration-design.md`
(commit `1dcdc2a`).
Implementation plan: `docs/superpowers/plans/2026-09-16-usb-host-enumeration.md`
(commit `39dc66c`).

Key design decisions:
- Use the STM32Cube HAL HCD API (`HAL_HCD_*`) directly from application code,
  bypassing Zephyr's `USB_HOST_STACK`/UHC subsystem entirely (no suitable
  backend exists for STM32H7).
- Keep the enumeration **state machine** hardware-independent (pure C,
  injected via a `struct usb_hcd_ops` function-pointer table) so it can be
  unit-tested on the host with plain `gcc`, without needing real hardware or
  the full Zephyr build.
- Do not move or duplicate PD10/VBUS ownership — leave Step 2's fix as-is.

## Step 4 — Task 1: Host-independent enumerator state machine (`commit bec054e`)

Files:
- `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.h` — public interface:
  `enum usb_enum_state` (idle → attached → resetting → addressing →
  described / detached / error), `enum usb_enum_error`, device descriptor
  struct, `struct usb_hcd_ops` (the hardware abstraction: `reset_port`,
  `get_speed`, `control_read`, `set_address`).
- `apps/zephyr-hello-shell/src/usb_host/usb_enumerator.c` — the actual state
  machine logic (`usb_enumerator_run()`): reset port → read 18-byte device
  descriptor at address 0 → `SET_ADDRESS` → re-read descriptor at the new
  address → parse VID/PID/class → `USB_ENUM_DESCRIBED`.
- `apps/zephyr-hello-shell/tests/usb_host/test_usb_enumerator.c` — 10 unit
  tests using a fake `usb_hcd_ops` (successful enumeration, reset failure,
  control-transfer timeout/stall, short/bad descriptor, overcurrent, detach
  mid-enumeration, etc).
- `scripts/run_usb_enumerator_tests.sh` — compiles+runs the test suite with
  plain `gcc -std=c99` (no Zephyr build system needed).

**TDD process actually followed:**
1. **RED:** Wrote `usb_enumerator.c` as an intentional stub (every function a
   no-op / returns failure). Ran
   `scripts/run_usb_enumerator_tests.sh` → failed at
   `test_successful_enumeration`'s `assert(ret == 0)` with a core dump —
   confirmed it failed for the *right* reason (stub always returns -1), not a
   compile error.
2. **GREEN:** Replaced the stub with the real implementation (reset → GET
   DESCRIPTOR → SET_ADDRESS → GET DESCRIPTOR again → parse). Re-ran the
   script: **all 10 tests passed.**
3. Committed.

## Step 5 — Task 2: STM32 HCD adapter

Files:
- `apps/zephyr-hello-shell/src/usb_host/stm32_hcd_adapter.h/.c` — implements
  `struct usb_hcd_ops` against the real `HAL_HCD_*` API: `HAL_HCD_Init`,
  `HAL_HCD_Start`, `HAL_HCD_ResetPort`, `HAL_HCD_HC_Init`,
  `HAL_HCD_HC_SubmitRequest`, `HAL_HCD_HC_GetState`,
  `HAL_HCD_HC_GetXferCount`, `HAL_HCD_GetCurrentSpeed`; registers
  `HAL_HCD_Connect_Callback`/`HAL_HCD_Disconnect_Callback`; owns the OTG_FS
  IRQ (IRQ 101, `OTG_FS_IRQn`).
- `apps/zephyr-hello-shell/Kconfig` (new) — added because
  `CONFIG_USE_STM32_HAL_HCD` and `CONFIG_USE_STM32_LL_USB` in the vendored
  Zephyr Kconfig have **no prompt** (`bool` with no `"..."` text), so they
  cannot be set directly from a `.conf` file — Kconfig errors with
  *"is assigned in a configuration file, but is not directly
  user-configurable"*. Fix: added an app-level `config APP_USB_HOST_HCD`
  symbol that `select`s the underlying non-prompted symbols, then set
  `CONFIG_APP_USB_HOST_HCD=y` in the board `.conf`.
- `apps/zephyr-hello-shell/CMakeLists.txt` — added the three new
  `usb_host/*.c` sources and a `target_include_directories` for
  `src/usb_host`.
- `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf` —
  added `CONFIG_APP_USB_HOST_HCD=y`.

**Build iteration (compile/link errors actually hit and fixed):**

1. First `--build-only` attempt failed at CMake/Kconfig stage:
   `USE_STM32_HAL_HCD ... is not directly user-configurable`.
   → Fixed by adding the `APP_USB_HOST_HCD` app Kconfig symbol described
   above (this is why the plan's original assumption of just setting
   `CONFIG_USE_STM32_HAL_HCD=y` directly in the board `.conf` didn't work).

2. Second attempt got past Kconfig but failed at **link** time: dozens of
   `undefined reference to USB_GetHostSpeed / USB_HC_Init / USB_CoreInit /
   ...` — these live in `stm32h7xx_ll_usb.c`, gated by a *separate* Kconfig
   symbol `CONFIG_USE_STM32_LL_USB` (also has no prompt). → Fixed by adding
   `select USE_STM32_LL_USB` to `APP_USB_HOST_HCD` as well.

3. Third attempt: **built and linked successfully** (276 KB flash, 62 KB
   RAM). Flashed via `--flash-only --skip-validate`; STM32_Programmer_CLI
   confirmed "Application is running."

## Step 6 — Task 3: Shell integration (`commit 5a54596`)

Files:
- `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c` — `board_usb_host_start()`
  (calls `stm32_hcd_adapter_init()`, then spawns a background polling
  thread that drains connect/disconnect events and drives
  `usb_enumerator_run()`); shell commands `usbhost status` and
  `usbhost descriptor`.
- `apps/zephyr-hello-shell/src/main.c` — added
  `extern int board_usb_host_start(void);` and a call to it near the end of
  `main()`, treated as **non-fatal** (LEDs/HTTP server keep running even if
  USB host init fails).

Built and flashed cleanly at this point (no new compile/link problems beyond
Step 5's).

## Step 7 — Task 4: Hardware validation (multiple bugs found and fixed live)

This is where the actual hardware behavior diverged from the plan's
assumptions, and each divergence was root-caused and fixed in turn.

### 7a. First hardware attempt: silent failure, "usb host init failed: -11"

After flashing and a user power-cycle, `usbhost status` printed
`usb host init failed: -11`. **This turned out to be a red herring** — `-11`
(`-EAGAIN`) is the *sentinel* value `usb_host_init_result` is initialized to
before `board_usb_host_start()` has actually run
(see `usb_host_shell.c`). Every time an **SWD-based reset** (soft reset via
`STM32_Programmer_CLI -c port=SWD mode=UR --rst`, or `-hardRst`) was used
instead of a genuine physical power-cycle, the serial console produced no
boot output at all and `usbhost status` kept reading this stale sentinel —
this is a known limitation of this board/setup (SWD resets do not reliably
restart application execution / re-enumerate the ST-LINK VCP), already seen
earlier in the session for the VBUS fix validation. **Lesson: always use a
real physical power-cycle to validate this board, never rely on SWD
`--rst`/`-hardRst` for application-level verification.**

### 7b. Real failure #1: `HAL_HCD_Init failed, status=1` (after a genuine power-cycle)

Once the user did a real power-cycle, the boot log showed:

```
usbhost: HAL_HCD_Init failed, status=1
USB host startup failed: -5
```

(`status=1` = `HAL_ERROR`; `-5` = `-EIO`, the errno the adapter maps
`HAL_ERROR` to.)

**Note on diagnosability:** Initially this failure was invisible because the
adapter used `LOG_ERR()`/`LOG_INF()`, but `CONFIG_LOG` is not enabled in this
app — those macros compiled to no-ops. **Fixed in the consolidated USB
implementation (`748763b`)** by
switching to plain `printf()`, which uses the app's already-enabled
`CONFIG_PRINTK`/`CONFIG_EARLY_CONSOLE` path. This is what made the real HAL
status code visible at all.

**Root cause investigation:** Read `HAL_HCD_Init()` in
`stm32h7xx_hal_hcd.c` → calls `USB_CoreInit()` → `USB_CoreReset()` (waits for
`GRSTCTL_AHBIDL` then toggles `GRSTCTL_CSRST`) → `USB_SetCurrentMode()`
(host mode). Two independent hardware-enablement issues were found and
fixed one at a time:

**Root cause A: USB 3.3V analog supply not enabled.**
STM32H7's internal USB PHY analog supply (`PWR->CR3.USB33DEN`) must be
enabled via `HAL_PWREx_EnableUSBVoltageDetector()` before the OTG core's
reset can complete, per `stm32h7xx_hal_pwr_ex.c`'s own comments and the
`PWR_FLAG_USB33RDY` flag. Added this call plus a poll loop for
`__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY)` before `HAL_HCD_Init()`. This also
required adding `select USE_STM32_HAL_PWR_EX` to the app's Kconfig (same
"no-prompt symbol" issue as Step 5) since `HAL_PWREx_EnableUSBVoltageDetector`
lives in `stm32h7xx_hal_pwr_ex.c`, gated by `CONFIG_USE_STM32_HAL_PWR_EX`.

Rebuilt, reflashed. This did **not** immediately resolve the symptom by
itself — still failed with `status=1` after this fix and before Root Cause B
was found and applied (both issues had to be fixed for `HAL_HCD_Init` to
succeed).

**Root cause B: USB kernel clock (48 MHz) never configured.** The OTG_FS
embedded full-speed PHY requires a 48 MHz "USB
kernel clock", selected via the RCC `D2CCIP2R`/`CDCCIP2R` `USBSEL` clock mux
(`LL_RCC_SetUSBClockSource()` in the LL RCC driver). Nothing in this app
configures this mux, because normally Zephyr's USB **device** stack driver
would do it — but that driver isn't enabled here (host-only build). Without
this clock, `USB_SetCurrentMode()` (called from inside `HAL_HCD_Init`) spins
in a 200 ms poll loop waiting for `USB_GetMode()` to reflect the host-mode
bit, times out, and returns `HAL_ERROR` — exactly matching the observed
`status=1`.

Fix: enable the internal HSI48 oscillator (`LL_RCC_HSI48_Enable()`, poll
`LL_RCC_HSI48_IsReady()`) and select it as the USB clock source
(`LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48)`), **before**
`HAL_HCD_Init()`. Required adding `#include <stm32h7xx_ll_rcc.h>` (build
failed once with `LL_RCC_USB_CLKSOURCE_HSI48 undeclared` before this include
was added — the compiler even suggested the wrong, ST HAL-style
`RCC_USBCLKSOURCE_HSI48` name that doesn't exist in the LL driver header
actually in use).

Built and flashed successfully (276.5 KB flash) after this fix.

**Status at end of session:** This fix was flashed
to the board but **not yet confirmed working** — a physical power-cycle and
fresh `usbhost status`/`usbhost descriptor` output from the user is still
needed to confirm `HAL_HCD_Init`/`HAL_HCD_Start` now succeed and that
enumeration of the attached camera actually completes.

## Consolidated commits

| Commit | What | Why |
|---|---|---|
| `1d9b12c` | Design and implementation plan | Scope USB host work to an enumeration-only milestone |
| `748763b` | Complete USB host implementation and tests | Configure STM32H755 OTG_FS, board VBUS, enumeration, control transfers, shell commands, and diagnostics |
| Documentation commit | Correct MB1363 references and debug record | Replace the mismatched MB1364 manual and preserve hardware findings |

## Step 8 — GPIO AF10 pinmux for OTG_FS pins: applied, but does not explain the CMOD stall

Investigated whether the real problem was that PA9 (SOF)/PA10 (`USB_FS_ID`)/PA11
(`USB_FS_N`/DM)/PA12 (`USB_FS_P`/DP) were never configured for their **AF10**
(`OTG_FS`) alternate function (confirmed via
`docs/rm0477-pinmux-pad-extract.md`, which lists `OTG_FS_ID`/`OTG_FS_DM`/`OTG_FS_DP`
under the AF10 column for these exact pins). Since `HAL_HCD_MspInit()` is a
`__weak` no-op and this adapter never overrides it (see `HAL_HCD_MspInit()` in
`stm32h7xx_hal_hcd.c`), nothing in the app was routing these pins to the OTG_FS
peripheral at all — a real, necessary gap.

Added `adapter_config_gpio()` in `stm32_hcd_adapter.c` (enables `GPIOA` clock,
sets PA10/PA11/PA12 to `LL_GPIO_MODE_ALTERNATE` / `LL_GPIO_AF_10`, PA11/PA12
push-pull very-high-speed no-pull, PA10 open-drain with pull-up), called at the
top of `stm32_hcd_adapter_init()`. Built and flashed (build
`2026-09-16T09:26:27Z`).

**Result: no change.** A clean, fully-captured boot log (see Step 9 below for
how this was finally captured) still showed:

```
usbhost: gpio configured
usbhost: HSI48 clock ready
usbhost: USB33RDY ready
usbhost: USB_CoreInit -> 0
usbhost: USB_SetCurrentMode -> 1 (GINTSTS=0x14000020)
USB host startup failed: -5
```

Same `GINTSTS=0x14000020` as before the GPIO fix. Decoded via
`USB_OTG_GINTSTS_*` bit definitions in `stm32h755xx.h`: bit0 (`CMOD`) = 0,
meaning `USB_GetMode()` still reports Device mode
(`USB_HOST_MODE`/`USB_DEVICE_MODE` = 1/0 per `stm32h7xx_ll_usb.h`) even after
`USB_SetCurrentMode()` forced `GUSBCFG.FHMOD` and spun for the full
`HAL_USB_CURRENT_MODE_MAX_DELAY_MS`. Bit28 (`CIDSCHG`, "Connector ID status
change") and bit5 (`NPTXFE`) were also set. **Conclusion: the GPIO pinmux fix
is still correct/necessary (D+/D- must be routed to the PHY for actual data
transfer to ever work), but it was not the cause of the CMOD/host-mode-forcing
failure** — that logic lives entirely inside the OTG core's digital state
machine and doesn't require the external pins to be routed for the mode bit
itself to flip.

## Step 9 — Debugging technique: validating over serial without a physical power-cycle

Multiple attempts this session to read fresh boot output immediately after an
SWD-triggered reset (`--rst`, `-hardRst`, or the reset performed automatically
by `--flash-only`) kept showing only `*** Booting Zephyr OS build ***` (Zephyr's
own kernel banner) followed directly by the `uart:~$` prompt — none of the
app's own `printf()` lines. This looked at first like the reset wasn't fully
re-running `main()` (matching the session's established "SWD resets are
unreliable" pattern), but turned out to have two separate causes, both
resolved:

1. **The CDC-ACM/VCP link can retain a backlog of already-sent bytes across a
   host-side serial port close/reopen.** Opening the port fresh and reading
   for a few seconds sometimes only drains a small tail of old buffered data
   (e.g. just the banner) rather than the fully fresh boot log, especially if
   an earlier probe attempt had already partially drained it. Sending a
   harmless command (e.g. `version`) after connecting and then reading forces
   a fresh prompt and reliably flushes/exposes what's actually pending.
2. **The real app-level init work in this adapter (HSI48 wait, USB33RDY wait,
   and especially `USB_HostInit()`'s FIFO-flush busy-loops inside
   `HAL_HCD_Init()`) can take several seconds to tens of seconds to resolve**
   (the flush loops spin up to `HAL_USB_TIMEOUT` = `0xF000000` iterations, a
   spin-count not a millisecond timeout). A `usbhost status` query issued only
   a few seconds after flashing can catch `-11` (`-EAGAIN`, the shell's
   not-yet-initialized sentinel) simply because `board_usb_host_start()`
   genuinely hasn't returned yet — not because anything is hung. Waiting
   ~30-45 s before querying resolved this every time.

**Working self-service validation recipe (no physical power-cycle needed),
used from here on:**

```python
import subprocess, threading, time, serial, sys

buf = bytearray()
stop = False
def read_serial():
    global buf, stop
    ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)
    start = time.time()
    while time.time() - start < 55 and not stop:
        d = ser.read(4096)
        if d:
            buf += d
    ser.close()

t = threading.Thread(target=read_serial)
t.start()
time.sleep(0.2)
subprocess.run(['scripts/flash_zephyr_hello_shell.sh', '--flash-only', '--skip-validate'])
time.sleep(40)   # let the (slow) USB host init finish before giving up
stop = True
t.join()
sys.stdout.buffer.write(bytes(buf))
```

Key points: open the serial port *before* invoking the flash script (so
nothing is missed), and keep reading for 40-55 s total — not just a few
seconds — since this app's USB init can be genuinely slow when HAL error
paths spin through their full timeout budgets. This produced the first fully
clean, end-to-end boot log of this build without needing the user to touch
the board.

## Step 10 — ID-pin hypothesis and later correction: `CIDSTS=1` is confirmed, but not the root cause

> **Correction (added later this session):** the document cited below,
> `docs/es0596-stm32h7rxx7sxx-device-errata-stmicroelectronics.md`, is
> actually the errata sheet for the **STM32H7Rx/H7Sx family**, not our
> actual MCU (**STM32H755**, dual-core Nucleo-H755ZI-Q). This was a
> mis-citation — the correct errata sheet for STM32H742/743/745/747/750/753/755/757
> (e.g. `ES0392`) was not available in `docs/` and could not be fetched from
> `st.com` in this environment (repeated timeouts). This does **not**
> invalidate the register observation itself: `GOTGCTL.CIDSTS=1` is a direct
> readout from our actual board. It **does invalidate the earlier claim that
> ID grounding was a confirmed STM32H755 requirement/root cause**. That
> conclusion was too strong and was based on an erratum that does not apply
> to this MCU family. The STM32H755 HAL explicitly supports forcing host
> mode through `GUSBCFG.FHMOD`; later diagnostics in Step 14 prove that the
> bit latches even while `CIDSTS=1`. ID grounding is therefore a valid
> hardware workaround/test, but is no longer considered mandatory or the
> confirmed root cause.

Searched `docs/es0596-stm32h7rxx7sxx-device-errata-stmicroelectronics.md` for
OTG-related known silicon issues and found **erratum 2.2.16, "Manual forcing
of OTG_FS host or device mode when not possible via ID pin (PM13)"**:

> When using the USB Full-Speed (FS) Host mode with manual host mode forcing
> (without ID pin), the internal pull-down resistors on the USB data lines
> (DP and DM) are disabled (incorrect behavior) if the ID pin is in high
> state. ... **The ID pin should be set to a low state for host mode** and
> set to a high state for device mode. ... The PM13 pin (PA10/`USB_FS_ID`)
> must be used only in ID mode, for no other functions including GPIO.

This matched the observed symptom (`GINTSTS` bit28 `CIDSCHG` set, `CMOD`
never flips to host mode), so it was tested directly rather than left as a
guess: added a diagnostic `printf` that reads `GOTGCTL.CIDSTS` (bit16 -
"Connector ID status": 0 = A-device/ID grounded, 1 = B-device/ID
floating-high) right before `USB_CoreInit()`. Rebuilt, reflashed, captured a
full clean boot log via the Step 9 recipe:

```
usbhost: GOTGCTL=0x03010000 (CIDSTS=1, B-device/ID-high/NOT-grounded)
usbhost: USB_CoreInit -> 0
usbhost: USB_SetCurrentMode -> 1 (GINTSTS=0x14000020)
USB host startup failed: -5
```

**What this actually confirmed:** the physical ID pin (PA10, CN13 pin 4)
was floating/high (`CIDSTS=1`) with the attached cable. It did **not**
establish that this caused the failed forced-mode transition. The original
log incorrectly labeled this the confirmed root cause before verifying
whether `GUSBCFG.FHMOD` itself latched or checking the correct STM32H755
errata.

**Why this happens:** the Micro-AB connector (CN13) only gets its ID pin
grounded by a genuine Micro-A plug or a cable/adapter explicitly built as a
"USB OTG host adapter" (these short pin 4 to GND inside the plug). A plain
Micro-B-style cable/adapter used to connect a standard USB-A device (like
this camera) to the board's Micro-AB port leaves ID electrically
disconnected, so it reads high (pulled up) — exactly `CIDSTS=1`.

Grounding ID with a genuine OTG adapter remains a useful A/B test and would
make the core auto-detect the host/A-device role, but it is **not the only
documented way to select host mode**: `USB_SetCurrentMode()` deliberately
sets `GUSBCFG.FHMOD` to force host mode. The unresolved question became why
the read-only `GINTSTS.CMOD` status remained device mode even though
`FHMOD` was written; Step 14 adds the register evidence.

## Step 11 — Board jumpers/DIP switches: checked, none found relevant to USB

> **Superseded in Step 16:** this investigation used UM2407/MB1364 after
> incorrectly identifying the physical board. The actual NUCLEO-H755ZI-Q
> board is MB1363 D-01 and uses UM2408; it has USB-relevant JP8. The text
> below is retained only as chronological history and must not be used for
> this board.

Checked whether any board jumper or DIP switch could be gating USB
functionality. `docs/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.md`
(the correct manual for our exact board, MB1364/NUCLEO-H755ZI-Q) documents
jumpers `JP1`-`JP6`, all related to ST-LINK reset/power-source selection,
`VDD_MCU` voltage selection (`JP5`, default `3V3`), and IDD current
measurement — **none gate the USER USB (CN13) connector or OTG_FS
functionality**. The board has **no on-board DIP switch block** (Nucleo-144
boards use jumpers, not DIP switches, for these settings) as documented in
UM2407.

One jumper is still worth a physical sanity check: **`JP5` must be on `3V3`,
not `1V8`** — if it were accidentally moved to `1V8`, the STM32H7 USB33
analog domain (`PWR->CR3.USB33DEN`, enabled in Step 7's fix) would not be at
the voltage the USB PHY needs, which could also present as USB failures.

The user separately pointed at `UM2408` ("STM32H7 Nucleo-144 boards MB1363"),
which is ST's manual for a **different board variant** (MB1363, not our
MB1364) — attempts to fetch it from `st.com` timed out repeatedly (ST's PDF
host appears to block/timeout this environment's fetch tool) and it was not
already present in `docs/`. Since MB1363 is a different PCB revision/board
from the one this repo targets, and UM2407 (the correct, already-available
manual for MB1364) shows no DIP switches or USB-relevant jumpers beyond
`JP5`, this line of investigation did not change the current hypothesis
(errata 2.2.16 / ID pin grounding, Step 10) — but it remains unverified
against UM2408 specifically if a future session has access to fetch it.

## Step 12 — LD7/LD8 both off: a second, independent hardware signal pointing at VBUS delivery

> **Superseded in Step 16:** the LD8 observation was useful, but the
> SB76/SB77 labels and active-high PD10 interpretation below came from the
> wrong MB1364 manual. The exact MB1363 schematic uses JP8/SB100 and proves
> PD10 active-low. No solder-bridge rework should be performed from this
> section.

The user reported that **both LD7 (red, USB overcurrent) and LD8 (green, USB
VBUS/host-active) are off** on the physical board, even with the PD10
gpio-hog fix (Step 2) applied and confirmed present in the running firmware.

Per `docs/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.md`
§7.6.6 ("USB OTG_FS"):

> A green LED (LD8) lights in one of these cases:
> - Power switch (U18) is ON and the STM32H7 Nucleo-144 board works as a USB Host
> - VBUS is powered by another USB Host when the STM32H7 Nucleo-144 board works as a USB Device
>
> The red LED (LD7) lights if overcurrent occurs when +5V is enabled on VBUS in USB Host mode.

Critically, **LD8 lighting depends only on the power switch (U18) being ON —
it is driven purely by the PD10 → U18 hardware path, with no dependency on
`USB_SetCurrentMode()`/`HAL_HCD_Init()` succeeding.** This means LD8 being
off is a *separate* symptom from the confirmed `CIDSTS=1` ID-pin issue (Step
10): even if the ID pin were grounded and enumeration worked perfectly, LD8
would still need PD10 to actually reach U18 to light up.

Per UM2407 Table (solder bridges) and §7.6.6 note 2:

- **`SB77` (PD10) must be ON** for "PD10 is connected to the USB power
  switch (U18) to control VBUS." If `SB77` is OFF, PD10 is instead routed
  only to the ST morpho connector (CN12) as a plain GPIO, and **never
  reaches U18 at all** — our firmware could be driving PD10 high correctly
  and LD8 would still never light, because the signal is physically
  disconnected from the power switch.
- **`SB76` (PG7) must be ON when using USB OTG_FS** (explicit note in
  §7.6.6) — this connects the overcurrent alarm signal; if off, PG7 is
  instead a plain GPIO on CN12. This gates the overcurrent *alarm/detection*
  path (relevant to LD7 behavior indirectly) but is not required for VBUS
  itself to turn on.
- **Table 12 ("USB pin configuration")** confirms: PA9=USB VBUS (`SB23`
  ON), PA10=USB ID (`SB24` ON), PA11=USB DM, PA12=USB DP, PD10=USB PWR EN
  (`SB77` ON), PG7=USB FS OVCR (`SB76` ON) — all six solder bridges are
  expected ON by default for "Configuration when using USB connector"
  (i.e. CN13/Micro-AB), per the same table.

**This is a second, independent potential hardware fault**, distinct from
the ID-pin/errata-2.2.16 finding:

- The **ID-pin problem (Step 10)** explains why *enumeration/host-mode
  negotiation* fails once VBUS is already present and the camera is drawing
  power.
- The **LD7/LD8-both-off symptom (this step)** suggests VBUS itself may
  never be reaching CN13 pin 1 at all — which would also explain why the
  camera shows no sign of life (no LED, no enumeration attempt from the
  camera's own perspective), independent of the ID-pin issue. If VBUS were
  actually present, LD8 should be lit regardless of whether
  `USB_SetCurrentMode()` succeeds, since LD8's condition ("power switch is
  ON") only requires PD10 to be driven and `SB77` to route it to U18 — no
  software-side USB stack success is required.

**This cannot be resolved or further diagnosed from firmware alone** — it
requires physical inspection of the board:

1. Visually confirm solder bridges `SB77` (PD10 → U18) and `SB76` (PG7,
   overcurrent alarm) are actually bridged/ON — these are 0Ω resistor
   pads/solder blobs on the board's top layer, not switches; a missing
   solder bridge here would silently break the PD10 → VBUS path even though
   the firmware fix in Step 2 is 100% correct.
2. With the board powered and the firmware's `board_early_usb_vbus_init()`
   fix running (PD10 driven high), measure voltage directly at CN13 pin 1
   (VBUS) with a multimeter — expect ~5V if `U18`/`SB77` are working. 0V at
   the pin with PD10 confirmed high at the MCU side would confirm a broken
   `SB77`/`U18` path rather than a firmware issue.
3. If `SB77` is found OFF (open), it needs to be bridged (soldered) for
   VBUS/PD10 control to work at all — this is a hardware rework, not
   something firmware can route around, since PD10 physically does not
   connect to U18 without it.

## Step 13 — UM2408/MB1363 table reveals PD10 jumper `JP8`

The user provided a screenshot of a **"Table 13. USB pin configuration"**
that was later identified as UM2408 for MB1363 (the actual board), not a
newer UM2407 revision. It differs from the incorrectly used MB1364 manual:

- Solder-bridge numbering differs from the local Rev 5 copy (e.g. PA10/ID
  shown as `SB29` there vs `SB24` in our local Rev 5 doc) — this is a known,
  expected effect of ST periodically renumbering solder bridges across
  manual revisions; it does not change any conclusion about the ID pin
  itself.
- **Crucially, `PD10` ("USB FS PWR EN") is shown gated by a jumper, `JP8`,
  not (only) by solder bridge `SB77`, and its required position is
  explicitly per-board-variant:**
  - `NUCLEO-H745ZI-Q` / **`NUCLEO-H755ZI-Q` (our board): `JP8` must be `ON`**
    when using the USB connector, `OFF` when using the ST morpho connector.
  - `NUCLEO-H7A3ZI-Q`: `JP8` must be `OFF` for both configurations (opposite
    polarity — different board variant, different jumper wiring).

**Why this matters for Step 12 (LD7/LD8 both off):** if this board's actual
silkscreen has a physical `JP8` jumper (in addition to/instead of solder
bridge `SB77`) and it is not populated in the `ON` position, **PD10 would
never reach the VBUS power switch (U18) regardless of anything the firmware
does** — `board_early_usb_vbus_init()` correctly drives PD10 high at the MCU
pin, but if `JP8` is open, that signal has nowhere to go. This is a strong,
concrete, easy-to-check candidate explanation for LD8 staying off.

**Action needed:** physically locate a jumper labeled `JP8` near the
PD10/USB power section of the board (in addition to the `SB77`/`SB76`
solder-bridge check from Step 12) and confirm it is populated in the `ON`
position for `NUCLEO-H755ZI-Q`. If missing or open, populate/move it to
`ON`.

The complete UM2408 Rev 6 PDF and Markdown conversion were later added to
`docs/`, together with the MB1363 H755ZIQ D-01 schematic (Step 16).

**Note: this does not provide new information about grounding the ID pin**
(Steps 10/12's subject) — `PA10`/ID is still shown gated only by a solder
bridge in this newer table too (`SB29` there), with no jumper alternative
for ID. The two remaining hardware action items (ground ID pin; verify VBUS
path) remain distinct and both still open.

### Physical result from this check

The user later confirmed that **`JP8` is ON**, so an open `JP8` is ruled out
as the explanation for LD8/VBUS being off. The user could not find an
`SB76` label on the physical board. This is consistent with the documentation
revision mismatch: the newer table uses different bridge/jumper numbering
than the local Rev 5 manual. Do not assume the local Rev 5 `SB76`/`SB77`
labels exist on this board revision; identify the actual board revision and
use its matching schematic/silkscreen before doing solder work.

## Step 14 — Re-evaluate forced host mode; prove `FHMOD` latches and test PA10 pull-up

An external answer correctly pointed out that the STM32 OTG core has a
software force-host control:

- `GUSBCFG.FHMOD` (bit 29): force host mode.
- `GUSBCFG.FDMOD` (bit 30): force device mode.
- `GINTSTS.CMOD` (read-only): reports the mode the hardware actually entered.

This is exactly what the STM32H7 HAL function
`USB_SetCurrentMode(USB_HOST_MODE)` does: clear `FHMOD`/`FDMOD`, set
`FHMOD`, then poll `GINTSTS.CMOD` in 10 ms intervals for up to the HAL
timeout. Therefore, the earlier statement that software could not force
host mode without grounding ID was incorrect for this MCU/HAL interface.

Added register diagnostics around `USB_CoreInit()` and
`USB_SetCurrentMode()` to distinguish two hypotheses:

1. `FHMOD` does not latch, indicating a write/clock/reset problem.
2. `FHMOD` latches, but another prerequisite prevents the core state machine
   from changing `CMOD`.

Fresh build/flash/serial result:

```text
Build time: 2026-09-16T11:10:45Z
usbhost: GOTGCTL=0x04c10000 (CIDSTS=1, B-device/ID-high/NOT-grounded)
usbhost: before CoreInit GUSBCFG=0x00001410 GCCFG=0x00000000 GRSTCTL=0x80000000
usbhost: USB_CoreInit -> 0 (GUSBCFG=0x00001440 GCCFG=0x00010000 GRSTCTL=0x80000000)
usbhost: USB_SetCurrentMode -> 1
         (GUSBCFG=0x20001440 FHMOD=1 FDMOD=0
          GINTSTS=0x14000020 CMOD=0)
USB host startup failed: -5
```

**Result:** `FHMOD=1` proves the force-host write succeeded and remained
latched; `FDMOD=0` is also correct. Nevertheless, `CMOD=0` remained device
mode until the HAL timed out. The failure is therefore **not** that the
application forgot to force host mode, and not that the register write was
lost. Some other core/PHY/reset prerequisite prevents the transition.

The GPIO setup had configured PA10/ID as AF10, open-drain, with an internal
pull-up. To test whether the application-created pull-up itself prevented
forced mode, changed only PA10 from `LL_GPIO_PULL_UP` to
`LL_GPIO_PULL_NO`, rebuilt, flashed, and captured another clean boot:

```text
Build time: 2026-09-16T11:12:18Z
usbhost: GOTGCTL=0x04c10000 (CIDSTS=1, B-device/ID-high/NOT-grounded)
usbhost: before CoreInit GUSBCFG=0x00001410 GCCFG=0x00000000 GRSTCTL=0x80000000
usbhost: USB_CoreInit -> 0 (GUSBCFG=0x00001440 GCCFG=0x00010000 GRSTCTL=0x80000000)
usbhost: USB_SetCurrentMode -> 1
         (GUSBCFG=0x20001440 FHMOD=1 FDMOD=0
          GINTSTS=0x14000020 CMOD=0)
USB host startup failed: -5
```

**Result:** removing the PA10 GPIO pull-up made no difference. `CIDSTS`
remained high (the connector/cable leaves ID floating), `FHMOD` still
latched, and `CMOD` still failed to change. This rejects the hypothesis that
our explicit PA10 pull-up was the cause. The PA10 pull-up was restored after
the rejected trial; the expanded register diagnostics were retained.

### External-reference checks performed during this phase

- STM32CubeH7 `STM32H747I-DISCO/Applications/USB_Device/MSC_Standalone`:
  device-mode MSC example on a different board/core configuration; no
  applicable ID/host-mode information.
- EEVblog "STM32H7 usb device detection refusal": unresolved 2019
  device-mode/VBUS-detection post; no replies or applicable host-mode fix.
- ST Community "STM32H743 nucleo won't work in USB OTG FS": full downloaded
  HTML showed the accepted ST answer was
  `HAL_PWREx_EnableUSBVoltageDetector()`. Multiple users confirmed it. This
  independently corroborates our already-applied USB33 voltage-detector fix
  in `748763b` but adds no new host-mode/ID information.
- The correct STM32H745/H755 errata sheet could not be fetched from ST in
  this environment. Do not use H7R/H7S erratum ES0596 as authoritative
  evidence for STM32H755.

## Step 15 — Root cause of `FHMOD=1`, `CMOD=0`: PA9 VBUS pin left in reset/analog mode

A generic host-mode checklist highlighted that host operation requires not
only HCD initialization and forced mode, but correct VBUS pin configuration.
The build was checked first:

- No Zephyr USB device/PCD/USBD/TinyUSB stack is enabled in the final
  `.config`.
- No `HAL_PCD_*`/`USBD_*` code owns the OTG_FS peripheral.
- The app calls only the HCD adapter and already sets
  `GUSBCFG.FHMOD` through `USB_SetCurrentMode(USB_HOST_MODE)`.

This ruled out a device-stack conflict. Review then found a concrete mistake
in `adapter_config_gpio()`: the comment called PA9 "SOF" and deliberately
left it unconfigured because HAL VBUS sensing was disabled. On this board,
however, **PA9 is `USB_FS_VBUS`** (PA8 is SOF). Leaving PA9 at its reset
analog-mode configuration prevented the OTG core from completing the
software-forced mode transition even though `FHMOD` latched.

Changed only PA9 initialization:

```c
LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_INPUT);
LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_9, LL_GPIO_PULL_NO);
```

Added read-only diagnostics for PD10 enable output/feedback and PA9 VBUS
input. Rebuilt, flashed, and captured:

```text
Build time: 2026-09-16T11:18:02Z
usbhost: init start
usbhost: gpio configured
usbhost: PD10 VBUS_EN ODR=1 IDR=1, PA9 VBUS sense=0
usbhost: HSI48 clock ready
usbhost: USB33RDY ready
usbhost: GOTGCTL=0x03010000 (CIDSTS=1, B-device/ID-high/NOT-grounded)
usbhost: before CoreInit GUSBCFG=0x00001410 GCCFG=0x00000000 GRSTCTL=0x80000000
usbhost: USB_CoreInit -> 0 (GUSBCFG=0x00001440 GCCFG=0x00010000 GRSTCTL=0x80000000)
usbhost: USB_SetCurrentMode -> 0
         (GUSBCFG=0x20001440 FHMOD=1 FDMOD=0
          GINTSTS=0x14000029 CMOD=1)
usbhost: USB_HostInit -> 0
usbhost: ready
```

This is the first successful HCD/core initialization on the real board:

- `FHMOD=1`, `CMOD=1`: forced host mode works with `CIDSTS=1`; ID grounding
  is not required for the mode transition on this STM32H755 setup.
- `USB_HostInit -> 0` and `usbhost: ready`: the previous startup `-5` is
  resolved.
- Live `usbhost status` returned `state: idle`, `error: none`: the stack is
  initialized but no powered device attach has been detected.
- `PD10 VBUS_EN ODR=1 IDR=1`: firmware is driving the VBUS-switch enable
  pin high and the MCU pin reads high.
- `PA9 VBUS sense=0`: VBUS is not present at the MCU VBUS-sense input.
  Together with LD8 remaining off, this moves the remaining fault to the
  board's PD10/JP8/U18/VBUS hardware path, not host-mode software.

**Root cause of the `USB_SetCurrentMode()` failure:** PA9 was left in its
reset/analog GPIO mode. Configuring PA9 as a no-pull digital input before
core initialization lets the OTG state machine acknowledge `FHMOD`.

## Step 16 — Correct MB1363 schematic: active-low VBUS and initial-attach race

The user added the exact board sources:

- `docs/um2408-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf`
  and generated sibling Markdown (UM2408 Rev 6).
- `docs/mb1363-h755ziq-d01_schematic.pdf` and generated sibling Markdown
  (MB1363, H755ZIQ, revision D-01).

The previously checked-in UM2407/MB1364 manual was for a different board and
was removed. This explains why `SB76`/`SB77` could not be found physically.
For this MB1363 board, UM2408 Table 13 specifies:

- PA9/VBUS: `SB21 ON`.
- PA10/ID: `SB29 ON`.
- PA11/DM: `SB26 ON`.
- PA12/DP: `SB27 ON`.
- PD10/USB FS PWR EN: `JP8 ON` for NUCLEO-H755ZI-Q.
- PG7/overcurrent: `SB100 ON`.

The D-01 schematic gives the decisive U18 detail:

```text
U18 = STMPS2141STR
USB_FS_PWR_EN:
  OFF: open drain
  ON:  0
```

Therefore **PD10 is active-low**: low enables VBUS; high disables VBUS.
The old `board_early_usb_vbus_init()`/gpio-hog interpretation was backwards.
The high level is still useful as a safe startup state, so it was renamed
and documented as VBUS-off. The adapter drives PD10 low only after
`HAL_HCD_Start()` has enabled host interrupts.

Controlled hardware trials:

```text
PD10=1 -> PA9=0, HPRT=0x00001000 (port powered internally, no device)
PD10=0 -> PA9=1, HPRT=0x00021401 (VBUS present, PCSTS=1)
```

This proves U18, JP8, CN13 VBUS, and PA9 sensing work. LD8 should light while
PD10 is low. It also disproves the prior hypothesis of a broken U18/VBUS
path.

When VBUS was enabled before the poll thread began, the hardware attach edge
could be consumed before the enumerator processed it. The adapter now checks
`HPRT.PCSTS` after VBUS settles and seeds `EVT_CONNECT` if a device is
already present.

Fresh result after seeding the existing connection:

```text
usbhost status
state: error
error: xfer_timeout
```

This is meaningful forward progress: host mode, VBUS, electrical attach,
interrupt handling, and enumerator dispatch all work. Enumeration now reaches
the first control transfer and times out there. The remaining bug is in host
channel/control-transfer handling, not board power, ID, or mode selection.

## Current status (end of this debugging pass)

- USB33 voltage detector fix: confirmed necessary, consolidated into
  `748763b`.
- `printf` over `LOG_*`: confirmed necessary for visibility, consolidated
  into `748763b`.
- HSI48 USB kernel clock fix: confirmed necessary (build compiles/links,
  clock reaches ready state), consolidated into `748763b` — but **not sufficient by
  itself** to get past `USB_SetCurrentMode`.
- GPIO AF10 pinmux for PA10-PA12: applied in `748763b`;
  confirmed not sufficient to change the `USB_SetCurrentMode` failure, but
  still required for DM/DP data transfer once mode switching works.
- `GOTGCTL.CIDSTS=1` confirms the attached cable leaves PA10/ID high, but
  **this is no longer considered the confirmed root cause**. The earlier
  conclusion relied on wrong-family erratum ES0596. STM32H755's HAL
  deliberately supports software force-host mode.
- **PA9 VBUS pin-mode fix resolves the forced-host transition:** fresh
  hardware logs show `FHMOD=1`, `CMOD=1`, `USB_SetCurrentMode -> 0`,
  `USB_HostInit -> 0`, and `usbhost: ready` (Step 15).
- Removing the application's PA10 internal pull-up did not change
  `CIDSTS`, `FHMOD`, `CMOD`, or the failure. That hypothesis is rejected.
- Exact MB1363 D-01 schematic proves U18 enable is active-low. PD10 low
  produces `PA9=1` and `HPRT.PCSTS=1`; VBUS hardware is working.
- PD10 now remains high/VBUS-off during core initialization, then goes low
  after HCD interrupts are active.
- Initial attached-device state is seeded from `HPRT.PCSTS`, avoiding a
  startup race before the poll thread begins.
- Live `usbhost status` now reaches `state: error`, `error: xfer_timeout`.
  Remaining blocker is the first USB control transfer.

## Open items / next steps (updated)

1. **Trace the first control-transfer timeout.** Instrument channel state,
   URB state, HCINT, HCTSIZ, and transfer count for the setup stage before
   changing transfer logic.
2. **Compare the custom control transfer against STM32 HAL host middleware**
   for channel allocation, PID toggles, packet count, and required status
   stage. Test one difference at a time.
3. **Do not modify/ground ID for mode forcing.** Step 15 proves software
   forcing works with `CIDSTS=1`; ID grounding is unnecessary for the
   resolved `CMOD` problem.
4. After each change, re-run the Step 9 self-service serial validation
   recipe (flash + wait
   30-45s + query `usbhost status` / `GOTGCTL` over serial) — it does not
   require a physical power-cycle for this class of change. Record
   channel/URB state and the first failing transfer stage.
5. Confirm `usbhost status` reaches `state: described` with the camera's
   VID/PID (HP camera, VID `0x05c8`, PID `0x0b06`, per an earlier `lsusb`
   check on the host side).
6. Re-run `scripts/flash_zephyr_hello_shell.sh --validate-only` once USB host
   is confirmed working, to make sure LEDs/Ethernet/HTTP/shell behavior is
   unaffected by the USB host changes.
7. Actual UVC video streaming (reading frames from the camera) is explicitly
   out of scope for this milestone and would be a separate, larger follow-up
   (would need a host-side UVC class implementation on top of this
   enumerator, plus isochronous transfer support in the HCD adapter, which
   `usb_enumerator_run()`/`stm32_hcd_adapter.c` do not currently implement).

## Step 17 — Match ST control sequencing and isolate persistent SETUP STALL

The original adapter submitted each host-channel transaction once and waited
only for `HC_XFRC`. STM32 HAL reports a NAK as `URB_NOTREADY`, so that
implementation converted a normal retry condition into `xfer_timeout`.
The adapter now:

- retries `URB_NOTREADY` and `URB_NYET` within one overall deadline;
- reports `URB_STALL` and `URB_ERROR` explicitly;
- performs the required zero-length OUT status stage after control reads;
- performs the required zero-length IN status stage after `SET_ADDRESS`;
- waits using the URB state rather than only the host-channel state.

Enumeration now follows ST's host middleware sequence more closely:

- wait 200 ms total after VBUS/device attachment before reset;
- perform the HAL 100 ms reset and wait another 100 ms afterward;
- request the first 8 bytes of the device descriptor;
- adopt the returned `bMaxPacketSize0`;
- request the complete 18-byte descriptor;
- send `SET_ADDRESS`, including its status stage.

ST's middleware initializes endpoint zero with MPS 64 and requests only eight
descriptor bytes first. A controlled MPS-8 trial did not change the result,
so the default was restored to 64.

The original timeout is fixed diagnostically: the first setup transaction now
ends as a real `URB_STALL`, not a software timeout:

```text
usbhost: setup failed HC_state=2 URB=4
HCINT=0x00000000 HCINTMSK=0x000004ff
HCTSIZ=0x60080008 HCCHAR=0x00100040
HPRT=0x00021405 PA9=1 PG7_nFAULT=1
```

Three complete reset/retry attempts produce the same result. `HCTSIZ` shows
one eight-byte SETUP packet with SETUP PID, while `HCCHAR` shows full-speed
control endpoint zero at address zero with MPS 64. `PA9=1`,
`HPRT.PCSTS=1/PENA=1`, and active-high `PG7_nFAULT=1` rule out absent VBUS,
disconnect, disabled port, and U18 overcurrent.

Additional rejected trials:

- Replacing free-running HSI48 with exact 48 MHz PLL3Q from the board's
  8 MHz HSE produced the identical STALL, ruling out USB clock accuracy.
- Calling aggregate `HAL_HCD_Init()` regressed the already-solved mode switch
  (`CMOD=0` and `HAL_ERROR`); the verified split initialization was restored.
- Driving PA10 low as an open-drain GPIO, and separately using AF10 with the
  internal pull-down, both left `GOTGCTL.CIDSTS=1`. Firmware GPIO
  configuration therefore cannot simulate grounding the connector ID input.
  PA10 AF10 with pull-up was restored.

Current boundary: firmware has verified host mode, VBUS, no overcurrent,
full-speed attachment, port reset/enable, correct SETUP PID/register fields,
ST-style timing, and retry behavior. The camera itself returns STALL to the
first standard `GET_DESCRIPTOR` request. The next useful discriminating test
is physical: use a cable/adapter that grounds connector ID, or temporarily
ground the PA10/ID net, then rerun the same firmware. If STALL remains, capture
D+/D- with a USB protocol analyzer or test another known-good low-power USB
device to distinguish signal integrity/camera behavior from host logic.

A second USB device was then connected and the existing image was reflashed
to guarantee a fresh attach/reset sequence. It produced the same three
first-SETUP `URB_STALL` results with the same healthy port and power signals:

```text
HPRT=0x00021405 PA9=1 PG7_nFAULT=1
HCCHAR=0x00100040 HCTSIZ=0x60080008 URB=4
```

This rules out a camera-specific refusal. The common failure is in the
board/cable/PHY host path. Because `CIDSTS` remains high for both devices and
neither GPIO output-low nor an internal PA10 pull-down can change it, a
physical ID-low test is now the highest-value next step.

## Step 18 — Physical ID grounding verified; SETUP STALL unchanged

With power removed, CN12 pin 33 (`PA10`/USB ID) was jumpered to CN12 pin 39
(digital ground). Firmware diagnostics were extended so `usbhost status`
reports both the GPIO input and OTG ID status.

Fresh hardware result:

```text
PA10_IDR: 0
CIDSTS: 0
state: error
error: xfer_stall
```

At the earliest pre-core diagnostic, `PA10_IDR=0` already proved the jumper
was physically connected while `CIDSTS` still held its reset/stale value of
1. After OTG initialization, repeated shell reads showed `CIDSTS=0`, proving
the OTG block eventually recognized the grounded ID input.

Enumeration nevertheless produced the same three endpoint-zero SETUP
`URB_STALL` results. Therefore physical ID-high is conclusively **not** the
cause of the current failure. The jumper may be removed; further diagnosis
should focus on the transmitted SETUP packet/signal path using a USB protocol
analyzer or comparison with ST's complete USB Host middleware.

## Step 19 — Replace the custom runtime enumerator with ST USB Host Core

To distinguish a bug in the custom control state machine from a lower-level
board/HCD problem, the firmware runtime now uses ST's complete generic USB
Host Core from
[`STMicroelectronics/stm32-mw-usb-host`](https://github.com/STMicroelectronics/stm32-mw-usb-host)
at commit `a5cc3798cd50f6b86322e7091ccf7e3eadd5be47`.

Imported components:

- `Core/Src/usbh_core.c`
- `Core/Src/usbh_ctlreq.c`
- `Core/Src/usbh_ioreq.c`
- `Core/Src/usbh_pipes.c`
- the corresponding `Core/Inc` headers and upstream license

No USB class implementation was imported. In particular, this is not yet a
UVC camera driver and cannot stream video. This A/B image exercises ST's
official attachment, reset, descriptor, address, and configuration state
machines only.

The project-specific `USBH_LL_*` bridge retains the board behavior already
proven in Steps 15-18:

- PA9 VBUS input and PA10/PA11/PA12 AF10 pin configuration;
- HSI48 and the USB33 analog supply;
- split `USB_CoreInit()` / forced-host / `USB_HostInit()` initialization;
- OTG_FS IRQ 101 and HAL HCD callbacks;
- active-low PD10 VBUS switching and existing-attachment recovery;
- PA10/CIDSTS, HPRT, PA9, PG7, and host-channel diagnostics.

The important protocol difference is that ST Core allocates separate EP0 OUT
and IN pipes instead of reconfiguring one channel for every control stage.
`USBH_Process()` runs from a 1 ms Zephyr thread with `USBH_USE_OS=0`.
`usbhost status` now reports the ST global, enumeration, and control states;
`usbhost descriptor` reads ST's parsed device descriptor.

The previous custom enumerator is no longer linked into firmware, so it
cannot contend for the HCD. Its host-native regression suite remains in the
repository and still passes all 11 tests.

Build and programming result:

```text
scripts/run_usb_enumerator_tests.sh
ALL TESTS PASSED

scripts/flash_zephyr_hello_shell.sh --build-only
exit 0

scripts/flash_zephyr_hello_shell.sh --skip-validate
File download complete
Start operation achieved successfully
```

The first programming pass intentionally skipped runtime validation. A
subsequent dedicated USB-only serial check allowed the long HCD initialization
to settle, then queried `usbhost status` and `usbhost descriptor`.

ST Core independently reproduces the original failure:

```text
ST USBH: USB Device Connected
ST USBH: USB Device Reset Completed
ST USBH: device connected
ST USBH: pipe 0 URB=4 HC_state=2
HCINT=0x00000000 HCINTMSK=0x000004ff
HCTSIZ=0x60080008 HCCHAR=0x20100040
HPRT=0x00021405 PA9=1 PG7_nFAULT=1
ST USBH: unrecovered error
ST USBH ERROR: Control error: Device not responding
```

Settled shell state:

```text
stack: ST stm32-mw-usb-host Core
PA10_IDR: 0
CIDSTS: 0
HPRT: 0x00021405
state: wait_for_attachment
enum_state: idle
control_state: error
connected: 1
port_enabled: 1

usbhost descriptor
no valid device descriptor
```

`URB=4` is HAL `URB_STALL`. `HCTSIZ=0x60080008` again shows the first
eight-byte SETUP packet with SETUP PID. `HCCHAR` identifies full-speed
endpoint zero at address zero with MPS 64; bit 29 varies with channel enable
state when sampled. The port remains connected and enabled, VBUS is present,
the power switch reports no fault, and physical ID grounding is recognized.

This result rules out the custom enumerator, its one-channel direction
switching, and its control-state sequencing as the cause. Both the custom
path and ST's complete middleware fail at the same first SETUP transaction.
Do not spend further effort rewriting the enumeration protocol. The next
diagnostic boundary is the physical USB data path:

1. Inspect/measure continuity through MB1363 solder bridges SB26 (DM) and
   SB27 (DP), including connector-to-MCU routing and absence of a D+/D− swap.
2. Capture the bus with a USB full-speed protocol analyzer. Confirm whether
   the host emits a valid reset and SETUP packet and whether the device
   actually transmits STALL, or whether the STM32/HAL is interpreting a
   malformed/noisy response as STALL.
3. If no analyzer is available, use a USB-capable oscilloscope to check idle
   J-state, reset SE0, packet activity, and signal integrity on both data
   lines. A normal logic analyzer is not sufficient for reliable USB FS
   protocol diagnosis.

A newly connected third USB device was then tested without rebuilding or
changing firmware. After the known 30-45 second HCD initialization window,
it produced the same result:

```text
PA10_IDR: 0
CIDSTS: 0
HPRT: 0x00021405
state: wait_for_attachment
control_state: error
connected: 1
port_enabled: 1

ST USBH: pipe 0 URB=4 HC_state=2
HCTSIZ=0x60080008 HCCHAR=0x20100040
HPRT=0x00021405 PA9=1 PG7_nFAULT=1

usbhost descriptor
no valid device descriptor
```

Thus three different USB devices have failed at the same first SETUP
transaction, and the latest two-device comparison uses ST's complete host
middleware. This further rules out device-specific behavior and strengthens
the case for investigating the common board/cable/PHY data path.

## Step 20 — Visual solder-bridge inspection: factory routing confirmed correct

A photograph of the board's solder-bridge silkscreen (bottom side) was
reviewed against the MB1363 default-configuration mapping already recorded
in this document (Step 16/18):

- `SB21` (PA9/VBUS): populated (0R bridge fitted).
- `SB26` (PA11/DM): populated.
- `SB27` (PA12/DP): populated.
- `SB29` (PA10/ID): populated.

All four are shown fitted from manufacture, matching UM2408's documented
default for USER USB (CN13) operation. This rules out an **unpopulated**
solder bridge as the explanation for the persistent SETUP STALL. It does
not rule out a cold/cracked joint or a trace-level defect under a
populated bridge, since that cannot be confirmed from a photograph — only
multimeter continuity/resistance measurement or reflow can confirm that.

Updated next step: since population is confirmed correct, prioritize
continuity measurement (bridge-to-connector-to-MCU pin) over resoldering,
and treat a USB protocol analyzer/scope capture as the next
highest-value action if continuity measures good.

## Step 21 — Checked for an official "as-is" CubeH7 example: none exists for this board

To go one level stronger than the ST-Core-middleware A/B test (Step 19),
we looked for an unmodified, official STM32CubeH7 USB Host *application*
project for our exact board, to flash and test completely outside Zephyr
and outside all of our own HAL bring-up code.

Checked the full `STMicroelectronics/STM32CubeH7` GitHub repository:

- `Projects/NUCLEO-H755ZI-Q` only contains a `readme.txt` referring to
  `NUCLEO-H745ZI-Q` for compatible examples.
- `Projects/NUCLEO-H745ZI-Q` has no `Applications/USB_Host` directory at
  all — no USB Host example exists for it.
- Repo-wide, `Applications/USB_Host/*` only exists for EVAL/Discovery
  boards (`STM32H743I-EVAL`, `STM32H745I-DISCO`, `STM32H747I-DISCO`,
  `STM32H747I-EVAL`, `STM32H750B-DK`) and the Nucleo-64 `NUCLEO-H723ZG`.
  **No Nucleo-144 board (H743ZI/H745ZI-Q/H755ZI-Q) has an official USB
  Host example.**

This confirms USB Host mode on the Nucleo-144 USER USB connector is an
unofficial/unsupported use case from ST's own test-coverage perspective —
consistent with everything already found in this investigation (ID
grounding jumper, active-low VBUS switch quirks, no CubeMX host template).

As the closest available official reference, vendored (source only) ST's
`NUCLEO-H723ZG/Applications/USB_Host/MSC_Standalone` into
`third_party/cubeh7-reference/` for a line-by-line HAL/HCD configuration
diff (see that folder's `README.md` for the full comparison table). H723ZG
uses the combo `USB_OTG_HS` peripheral in FS-PHY mode (different silicon
block than our board's dedicated `USB_OTG_FS`), so it cannot be flashed
onto our board as a true "as-is" test — it was used purely as a
configuration reference.

**Result: no missed initialization step found.** Every comparable
HCD/GPIO/clock setting (`dma_enable`, `phy_itface`, `low_power_enable`,
`vbus_sensing_enable`, `speed`, D+/D− AF10 config, active-low VBUS switch
polarity) matches between our code and ST's own reference. The one
difference — our code configures PA9 as a no-pull input in MSP init while
the reference doesn't touch its equivalent pin — is explained by the
different peripheral (`USB_OTG_FS` vs `USB_OTG_HS`-in-FS-mode) and was
already empirically confirmed necessary on our hardware (Step 7-era
finding: `GINTSTS.CMOD` never flips to host mode without it).

This closes out the firmware/HAL-configuration diagnostic avenue: custom
code, ST's official middleware, and now ST's own official low-level
reference all agree our configuration is correct. The physical D+/D−
path (SB26/SB27 continuity, cable/connector, or a scope/protocol-analyzer
capture) remains the only unexplored explanation for the persistent
first-SETUP STALL/ERROR.

## Step 22 — Software GPIO-based D+/D- capture (poor-man's logic analyzer)

Before requiring external scope/protocol-analyzer hardware, we tried
answering "can this be captured in software" using only the MCU itself.

Implemented `apps/zephyr-hello-shell/src/usb_host/usb_gpio_capture.c/.h`:
a tight loop that samples `GPIOA->IDR` (bits 11/12 = D-/D+) as fast as
the CPU can execute, with interrupts disabled for determinism and a DWT
cycle-counter timestamp. New shell commands:

- `usbhost capture [n]` — free-running snapshot capture (not
  synchronized to any transaction).
- `usbhost arm` — arms a **synchronized** one-shot capture that fires
  from inside `USBH_LL_SubmitURB()` (`stm32_hcd_adapter.c`) the instant a
  SETUP-token URB is submitted to the hardware, so the sample window
  starts right as the real electrical transaction is triggered instead of
  at an arbitrary later moment.

No DMA/timer peripherals are touched, so this cannot interfere with the
already-working HCD configuration — purely a CPU busy-loop instrumentation
addition.

### Achieved rate and results

Measured **~30 MHz effective sample rate** (33 ns/sample, calibrated via
`DWT->CYCCNT` against `SystemCoreClock`=480 MHz) — borderline-adequate
oversampling for 12 Mbit/s full-speed USB (full NRZI+bit-stuff decode
wants ~48 MHz, but ~30 MHz is enough to see coarse J/K/SE0 line-state
transitions).

Two synchronized (`usbhost arm`) runs, sizes 4096 and 32768 samples
(≈137 µs and ≈1.09 ms windows respectively — the larger window
deliberately spans more than a full 1 ms USB frame period, to rule out
missing the transaction due to internal host frame-scheduling latency
after channel-enable):

```
samples: 32768, cycles: 524353, core_clock_hz: 480000000
avg_ns_per_sample: 33
idx=0 DP=0 DM=0 (initial)
total_transitions: 0
```

**Both D+ and D− read a constant logic-0 (SE0) for the entire window,
including the very first "idle" sample before any transaction — zero
transitions observed, across both window sizes.**

### Important caveat — do not over-interpret this result

PA11/PA12 are configured as `AF10` (routed into the embedded `OTG_FS`
PHY), not as plain GPIO inputs. It is **not guaranteed** that the
standard digital input register (`IDR`) faithfully tracks the analog USB
differential-PHY receive state while a pin is muxed to this alternate
function — on some STM32 parts the digital input path for
dedicated PHY-routed pins can be disconnected, forced to a fixed level,
or simply not meaningful in this mode. A constant-zero readback is
therefore *consistent with* a dead/broken D+/D− path, but could equally
be an artifact of this software-capture technique's limitation rather
than proof of an electrical fault.

### Conclusion

This software capture is a genuinely useful low-effort diagnostic (no
extra hardware, git-committed as a reusable tool) and its result does not
contradict the hardware-fault hypothesis — but it cannot be treated as
final confirmation given the AF10/IDR caveat above. An external
oscilloscope or USB protocol analyzer probing the physical D+/D− test
points directly remains the authoritative next step to conclusively
resolve whether the bus is truly electrically dead or whether this is a
software-visibility limitation.

## Step 23: Full pinmux/AF cross-check for all USB-related pins

Requested review: re-derive every USB-host-related pin's alternate
function assignment straight from the datasheet AF table
(`docs/rm0477-pinmux-pad-extract.md`, Table 10, extracted from
`stm32h743vi.pdf`) and compare it, pin-by-pin, against what
`adapter_config_gpio()` in `stm32_hcd_adapter.c` actually programs.

Pins involved (all on the CN13 USER USB / OTG_FS path per UM2408 and
MB1363 D-01):

| Pin  | Signal            | Datasheet AF (Table 10) | Firmware config | Match? |
|------|-------------------|--------------------------|-----------------|--------|
| PA8  | OTG_FS_SOF (unused)| AF10                    | not configured (SOF not used by this app) | N/A, not required for enumeration |
| PA9  | OTG_FS VBUS sense | no AF — plain analog/digital input pin (H7 OTG_FS FS core has no dedicated VBUS-sense AF; VBUS is read as GPIO) | `MODE_INPUT`, `PULL_NO` | ✅ |
| PA10 | OTG_FS_ID         | **AF10**                | `AF_10`, `MODE_ALTERNATE`, `PULL_UP`, open-drain | ✅ |
| PA11 | OTG_FS_DM (D−)    | **AF10**                | `AF_10`, `MODE_ALTERNATE`, `PULL_NO`, push-pull, very-high speed | ✅ |
| PA12 | OTG_FS_DP (D+)    | **AF10**                | `AF_10`, `MODE_ALTERNATE`, `PULL_NO`, push-pull, very-high speed | ✅ |
| PD10 | VBUS enable (active-low, board-level MOSFET/load-switch enable) | n/a — plain GPIO output, no AF | Zephyr `gpio-hog` in board overlay, `output-high` = disabled by default, driven low by app to enable VBUS | ✅ |
| PG7  | Overcurrent fault (nFAULT) | n/a — plain GPIO input, no AF | `MODE_INPUT`, `PULL_NO` | ✅ (note: no internal pull-up; relies on external pull-up per schematic, not verified but doesn't affect D+/D-) |

Method: read the raw AF0–AF15 table rows for PA8–PA12 directly out of
the extracted datasheet text, aligning each row's 16 tokens against the
`AF0..AF15` header row to identify which column is OTG_FS-related for
each pin (confirmed via PA8, whose AF10 slot is unambiguously
`OTG_FS_SOF`, anchoring "AF10 = OTG_FS block" for this GPIO port/pin
group). Cross-referenced against `LL_GPIO_SetAFPin_8_15(...,
LL_GPIO_AF_10)` calls in the firmware for PA10/PA11/PA12.

**Result: every pin's alternate-function number, mode, pull, and output
type matches the datasheet exactly.** No mismatch, no swapped AF index,
no missing alternate-function assignment was found. This corroborates
the Step 21 finding (config diff against ST's own H723ZG reference
example showed no discrepancy) from an independent angle (raw datasheet
table instead of a reference project).

### Conclusion

The pinmux/AF configuration for all USB-host-related pins is
byte-for-byte correct per the datasheet. This rules out a pinmux
misconfiguration as the root cause. Combined with Steps 1–22 (VBUS
present, ID pin correctly grounded/read, HPRT/HCD registers showing
correct reset/enable sequencing, HCTSIZ showing correctly-sized 8-byte
SETUP transfers, solder bridges populated, no missed HAL init step vs.
ST's own reference, and now a clean pinmux audit), the remaining
plausible root causes are narrowed almost entirely to the physical
layer: a bad solder joint/trace/ESD-protection component on the D+/D−
path between the connector and PA11/PA12, or a damaged internal
transceiver — not anything discoverable by further code/config review.
An external oscilloscope or protocol analyzer capture at the physical
test points remains the next authoritative step.

## Step 24: Direct evidence — this exact board successfully hosts USB in ST Community

Requested: search community.st.com for a similar issue. Approach:
`search.brave.com` was rate-limited (429) for the entire session this
time, and every other public search engine tried (Bing, DuckDuckGo,
Ecosia, Mojeek, Startpage, Yandex, marginalia, multiple SearXNG
instances) was blocked, JS-gated, or unusable for scripted queries.
Worked around this by downloading ST Community's own `sitemap-topics-
{1..4}.xml` (~26 MB total, ~163k thread URLs) directly and grepping
locally for USB host/H745/H755-related thread slugs.

Found: **`usb-host-on-stm32h755zi-q-cdc-class-not-detected-for-usb-to-
ttl-device-154603`** — same exact board (STM32H755ZI-Q Nucleo), same
exact mode (USB Host). The poster reports:

> "I've successfully tested the USB Host functionality using standard
> USB devices such as a keyboard and a mouse, both of which are
> recognized correctly by the host."

And for a CH340 USB-to-TTL adapter (VID 1a86h), their log shows a
**fully successful enumeration**: address assigned, manufacturer/
product descriptor strings read, configuration set, "Enumeration
done." Their only remaining issue is "No registered class for this
device" — i.e., missing CDC class driver registration, a much later
and purely software/class-support gap, not an electrical/protocol
failure.

Other near-miss threads checked and ruled out as not applicable:
- `usb-host-stall-96907` (2015, F407 Discovery): an old MW USB Host
  library bug where `USBH_HandleControl`'s `CTRL_DATA_IN_WAIT` case
  loops forever on `USBH_URB_STALL` instead of returning to the
  previous state. Different symptom (software hang on STALL vs. our
  clean but consistently negative STALL/ERROR response) and a much
  older/different library version; not applicable.
- `stm32h743-usb-otg-fs-not-responding-to-core-reset-79012`: `GRSTCTL_
  CSRST` (core soft reset) never self-clears on a Nucleo-H743. Different
  failure point (core reset itself times out during `HAL_PCD_Init`,
  before any device is even attached) — we get well past core reset,
  VBUS enable, ID-pin detection, and port-enable in our case, so not
  applicable.
- `problem-with-usb-otg-again-144246` (checked in a prior request):
  H723ZG **Device**-mode VCP with a custom no-VBUS-wire cable; unrelated
  failure mode.

### Why this matters — re-evaluating the "ST-unsupported" theory

Step 21 concluded that the total absence of an official CubeH7
`Applications/USB_Host` example for any Nucleo-144 H7 board hinted that
USER USB host mode might be an ST-unvalidated use case on this hardware
family. **This new evidence weakens that theory significantly**: a
different user, on the identical board (H755ZI-Q), using the identical
USER USB connector in host mode, gets full enumeration working for
multiple real devices (keyboard, mouse, CH340 USB-to-TTL) — with only a
missing class-driver gap remaining, nothing electrical or protocol-
level. This is direct proof the hardware path (VBUS, ID, D+/D−, PHY,
HCD/core reset, enumeration protocol) is capable of working correctly
on this exact board model.

### Revised conclusion

Since another unit of the same board model demonstrably works, our
board's failure is now more likely explained by one of:
1. **A defect specific to our individual board** (cold solder joint,
   damaged trace/pad, ESD component failure on D+/D− between CN13 and
   PA11/PA12) — consistent with all steps so far, and now *more*
   likely given a sibling board works fine.
2. A subtle difference in our own firmware/init sequence not present
   in the working poster's setup (though Steps 1–23 found no config or
   pinmux discrepancy vs. ST's own reference).

This makes an external oscilloscope/protocol-analyzer capture at the
physical D+/D− test points (or a straightforward continuity/resistance
check across SB26/SB27 and the CN13 connector pins) even more valuable
as the next step — it can now be interpreted with higher confidence
that a negative (dead-bus) result means genuine local hardware failure,
not a fleet-wide board/software limitation.

## Step 25: Compare against a working, publicly-shared H755ZI-Q USB project

User pointed to
`https://github.com/st9540808/NUCLEO-H755ZI-Q-USB-OTG-CDC` — a real,
working CubeMX 6.6.1-generated project for the same board that gets a
CDC virtual COM port enumerated on the PC through CN13 (USER USB).

**Note:** this project runs `OTG_FS` in **Device** mode
(`VirtualMode=Device_Only` in the `.ioc`), not Host mode, so it can't be
used as a drop-in host-mode reference. But since Device and Host mode
share the exact same physical pins/PHY/silicon block, its GPIO/clock
config for those shared pins is still a useful independent check.

Cloned (`--depth 1`) and inspected `CM7/USB_DEVICE/Target/usbd_conf.c`
(`HAL_PCD_MspInit`) and the `.ioc` file:

| Setting | Their project (working, Device mode) | Ours (Host mode) | Match? |
|---|---|---|---|
| PA8 (SOF) | AF10, `GPIO_MODE_AF_PP`, no pull | not configured (unused) | N/A — SOF not required for enumeration in either mode |
| PA9 (VBUS sense) | plain `GPIO_MODE_INPUT`, no pull | plain `LL_GPIO_MODE_INPUT`, no pull | ✅ |
| PA11 (D−) | AF10, `GPIO_MODE_AF_PP`, no pull, `GPIO_SPEED_FREQ_LOW` | AF10, alternate/push-pull, no pull, `SPEED_FREQ_VERY_HIGH` | Only the GPIO speed differs (LOW vs VERY_HIGH) |
| PA12 (D+) | AF10, `GPIO_MODE_AF_PP`, no pull, `GPIO_SPEED_FREQ_LOW` | AF10, alternate/push-pull, no pull, `SPEED_FREQ_VERY_HIGH` | Same speed difference as PA11 |
| USB kernel clock | `RCC_USBCLKSOURCE_HSI48`, 48 MHz | `LL_RCC_HSI48_Enable()`, same source | ✅ |
| `HAL_PWREx_EnableUSBVoltageDetector()` | called | called (see `stm32_hcd_adapter.c` near line 145) | ✅ |
| PD10 (VBUS enable) | plain GPIO output, label `USB_OTG_FS_PWR_EN` | plain GPIO output (Zephyr `gpio-hog`, active-low enable) | ✅ (same board-level signal) |
| PG7 (overcurrent) | plain GPIO input, label `USB_OTG_FS_OVCR` | plain GPIO input, no pull | ✅ |

**Only difference found: GPIO output speed on PA11/PA12** — CubeMX's
Device-mode default uses `GPIO_SPEED_FREQ_LOW`, while our Host-mode
code uses `SPEED_FREQ_VERY_HIGH`. This is very unlikely to be a root
cause: higher slew-rate/speed settings are, if anything, more permissive
for a full-speed (12 Mbit/s) signal, not less — `VERY_HIGH` should
produce a cleaner/faster edge, not a broken one. Everything else
(alternate function number, pull configuration, output type, VBUS
pin handling, VBUS voltage detector enable, kernel clock source)
matches exactly.

### Conclusion

This is now the **third independent confirmation** (Step 21 vendored
H723ZG Host example, Step 23 raw datasheet AF-table audit, and now this
real working H755ZI-Q Device-mode project) that our GPIO/pinmux/clock
configuration for the shared USB pins is correct and matches known-good
reference code. Combined with Step 24 (a different H755ZI-Q board
successfully doing full Host-mode enumeration), the software/config
side of this investigation is now about as exhausted as it can be
without new instrumentation. The evidence continues to converge on a
**board-specific physical-layer fault** (most likely on our unit's
D+/D− path) as the most probable remaining explanation.

## Step 26: BREAKTHROUGH — USER USB physical layer proven 100% alive via Device-mode test

User's insight: since a PC (a known-good USB host) was now connected to
CN13, we could flash a **stock, unmodified Zephyr USB Device sample**
(`samples/subsys/usb/cdc_acm`) onto the exact same board/connector/PHY
and let the PC itself act as the reference host — sidestepping every
assumption baked into our custom Host-mode code entirely.

### What was done

1. Built `samples/subsys/usb/cdc_acm` (stock Zephyr sample, zero
   modifications) for `nucleo_h755zi_q/stm32h755xx/m7` — this board's
   Zephyr port already wires `zephyr_udc0` to `&usbotg_fs` on
   PA11/PA12 (confirmed earlier in the board `.dts`), so no devicetree
   changes were needed.
2. Flashed it via the same `STM32_Programmer_CLI`/SWD path used
   throughout this investigation.
3. Connected the same CN13 (USER USB) connector directly to this
   machine's own USB port (a real PC host) with a standard cable.

### Result

```
$ lsusb
Bus 003 Device 026: ID 2fe3:0001 NordicSemiconductor Zephyr CDC ACM sample
```

Full, clean enumeration: correct device descriptor (bDeviceClass 239/
Miscellaneous, IAD), manufacturer/product/serial strings ("ZEPHYR",
"Zephyr CDC ACM sample", a real serial number), CDC-ACM configuration
descriptor parsed correctly, and Linux bound `cdc_acm` and created
`/dev/ttyACM1`.

**Bidirectional data-path test** (not just EP0 control transfers —
exercises the bulk IN/OUT endpoints too):

```python
s = serial.Serial('/dev/ttyACM1', 115200, timeout=2)
s.write(b'Hello USB!\r\n')
# read back:
b'Hello USB!\r\n'
```

The sample's echo behavior worked perfectly — full round-trip data
through the exact same D+/D−, PHY, solder joints, and connector that,
under our Host-mode firmware, showed constant SE0 with the GPIO capture
tool (Step 22).

### This conclusively overturns the Steps 20–23/25 "board hardware
fault" hypothesis

Everything this investigation attributed to a possible dead/damaged
physical D+/D− path (cold solder joint, broken trace, damaged PHY, bad
ESD component) is now disproven for **this specific board**: the exact
same electrical path carries a fully valid USB Full-Speed signal,
completes chirp/reset/enumeration, and sustains bulk data transfer, as
long as the board is in **Device** mode.

This means:
- VBUS, ID pin, D+/D−, the OTG_FS PHY, the solder bridges, CN13 itself,
  and all board-level wiring are **all electrically sound**.
- The root cause of the original failure is now isolated with high
  confidence to the **Host-mode (HCD) code path** — either in our
  custom `stm32_hcd_adapter.c` glue, ST's HAL/LL HCD driver behavior on
  this MCU/board combination, or some interaction between them (e.g.,
  the SETUP-token transmission/formation itself, FIFO configuration, or
  a register-level HCD misconfiguration that only manifests once a real
  transaction is attempted) — **not** a hardware defect.
- The Step 24 finding (another H755ZI-Q board successfully Host-mode
  enumerating keyboard/mouse/CH340) is now fully consistent with this:
  the hardware family is capable; something specific to our *firmware's
  host-mode implementation* is not completing the transaction, even
  though the wire is good.

### Immediate implication for next steps

This entirely redirects the investigation: external oscilloscope/
protocol-analyzer work is no longer the top priority (hardware is
proven good). Instead, the priority becomes:
- Line-by-line audit of the HCD channel/FIFO setup path in
  `stm32_hcd_adapter.c` and comparison against exactly how Zephyr's own
  `usb_dc_stm32.c`/HAL PCD path configures the same OTG_FS core in
  Device mode, since that path demonstrably works on this silicon.
- Trying **Zephyr's own upstream USB Host support** (if/when available
  for this SoC) instead of the hand-rolled `stm32_hcd_adapter.c` +
  ST middleware bridge, since our custom glue code remains the one
  unproven, never-independently-validated layer in this whole stack.
- Revisiting HCD-specific register setup (`HCTSIZ`, `HCCHAR`, FIFO
  RAM allocation, `GRXFSIZ`/`GNPTXFSIZ`/`HPTXFSIZ`) with fresh
  suspicion, now that "the wire is broken" is off the table.

### Board state note

The board is currently flashed with this stock CDC-ACM **Device**-mode
test firmware, not the project's normal Host-mode
`apps/zephyr-hello-shell` image. Re-flash
`scripts/flash_zephyr_hello_shell.sh` to restore the normal Host-mode
application when resuming host-mode debugging.
