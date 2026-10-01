# USB Host Bring-Up Debug Log (Nucleo-H755ZI-Q, `apps/zephyr-hello-shell`)

This document is a chronological record of USB bring-up, starting from
"why doesn't the USB camera LED turn on." Earlier failures below describe
the state *at that step*, not the current mouse result.

## Current mouse functionality

The Dell MS116/PixArt boot mouse (`413c:301a`) connected directly to USER
USB CN13 **works in the normal Zephyr application**. Run `usbhost resume`
on the ST-LINK UART, move/click the mouse, then run `usbhost status`.
The class decodes movement, buttons, and wheel from the 4-byte
interrupt-IN reports; status shows the cumulative `mouse: reports=`
count and last decoded report. The board does not expose the mouse as a
Zephyr input device or move a host-side pointer; the data is available
through this class and UART diagnostics.

The normal-firmware live capture decoded 919 movement/button lines
and counted 1,028 reports with no interrupt-IN transfer errors.
Four fresh resets of the minimal Zephyr image got first mouse IRQ
`NAK`, not `TXERR`, and a live capture counted 1,358 reports including
button input. The host uses the STM32 HAL HCD interrupt and ST USB
Host middleware, but advances the middleware at a continuous 50 us
loop in `HOST_CLASS`. A 1 ms class-loop sleep reproduced first-event
`TXERR` on 3/3 crossover resets. This fast processing is a
CPU-intensive **workaround** for a timing-sensitive integration, not
proof that the hardware must be polled at 20 kHz or that TXERR can
never occur. See Steps 65-66 and the crossover experiment README.

The earlier full-speed flash-drive enumeration also works. **Webcam
video has not been demonstrated**: the HP camera has separate
enumeration failures, and this application has no host-side UVC
class or video transfer implementation.

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

## Step 27: Why the CN12 ID jumper didn't matter for the Device-mode test

User asked a sharp follow-up: the CN12 pin33↔39 ID-ground jumper (Step
18) was never removed, yet the stock Device-mode test in Step 26 still
worked. How?

Answer, confirmed directly in HAL source
(`stm32h7xx_ll_usb.c:USB_SetCurrentMode()`):

```c
USBx->GUSBCFG &= ~(USB_OTG_GUSBCFG_FHMOD | USB_OTG_GUSBCFG_FDMOD);
if (mode == USB_HOST_MODE)   { USBx->GUSBCFG |= USB_OTG_GUSBCFG_FHMOD; }
else if (mode == USB_DEVICE_MODE) { USBx->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD; }
```

Both our custom Host-mode code and Zephyr's stock `cdc_acm` Device-mode
sample call this same function, which **unconditionally forces** the
OTG core into host or device role via the `FHMOD`/`FDMOD` bits in
`GUSBCFG` — the analog ID-pin comparator (`GOTGCTL.CIDSTS`) is never
read or branched on by either code path. ID-pin-based role detection
only matters for genuine dual-role OTG negotiation (SRP/HNP), which
neither firmware implements.

This is consistent with the Step 14 finding that `FHMOD` latches even
while `CIDSTS=1`. It confirms the CN12 ID jumper has **no effect on
mode selection in this codebase, in either direction** — it may be
left in place or removed with no expected change to either the Host-
or Device-mode firmware's behavior.

## Step 28: Re-confirmation, ID jumper decision, and forward debugging plan

### Re-confirmation (next day, fresh session)

Re-checked the Device-mode CDC-ACM test firmware from Step 26 in a new
session, without reflashing anything:

```
$ lsusb
Bus 003 Device 049: ID 2fe3:0001 NordicSemiconductor Zephyr CDC ACM sample
```

Device number changed (026→049 — some reconnect/power-cycle happened
between sessions) but it re-enumerated cleanly on its own. Re-ran the
bidirectional echo test:

```
sent:     'still alive?'
received: 'still alive?'
```

Still works. This further reinforces Step 26: the USER USB physical
path is reliably functional in Device mode, not a one-off fluke.

### Does the Device-mode success prove Host mode hardware works too?

Mostly yes, with one nuance. Device and Host mode share the identical
analog path: D+/D−, the embedded OTG_FS PHY, CN13, and the solder
joints/traces to PA11/PA12 — a broken/dead transceiver could not
selectively work in only one mode. That rules out a dead-bus hardware
explanation.

What Device mode does *not* exercise: VBUS *drive* out to an attached
device (PD10 + load switch — Device mode only *senses* VBUS via PA9),
and host-specific pull-down/chirp logic. However, our own **prior**
Host-mode logs (well before Step 26) already independently showed
VBUS drive present (`PA9=1`), `HPRT` reporting a correctly reset/
enabled port, before the SETUP-token STALL occurred — so those
host-specific circuits were already confirmed working on their own.
Combined, this gives high confidence the *entire* electrical/physical
path required for Host mode is sound on this board. The remaining
fault is isolated to the Host-mode firmware/HCD software path.

### CN12 ID jumper (pin 33 ↔ 39): decision — leave it connected

Per Step 27, `USB_SetCurrentMode()` in the vendored HAL
(`stm32h7xx_ll_usb.c`) forces the OTG core's role unconditionally via
`GUSBCFG.FHMOD`/`FDMOD` and never reads `GOTGCTL.CIDSTS` — confirmed
directly from source, not inferred. Per Step 18, physically grounding
vs. floating the ID pin already made zero difference to the Host-mode
STALL outcome. **Decision: leave the jumper in place for all further
debugging steps below.** It has no bearing on any of them, and removing/
reinstalling it only risks introducing an unrelated variable.

### Forward debugging plan — proving/fixing USB Host mode

Zephyr was checked for a native USB Host Controller (UHC) driver for
this SoC first, since Step 26 showed Zephyr's own Device stack works
correctly here — if a stock Zephyr Host stack existed for STM32 OTG_FS,
repeating the "use ST's own/upstream code as-is" trick from Step 26
would be the fastest possible test. **Result: no such driver exists.**
`drivers/usb/uhc/` only implements NXP MCUX controllers
(`uhc_mcux_*.c`) and the external SPI `MAX3421E` chip
(`uhc_max3421e.c`) — there is no `uhc_stm32*.c`. So Zephyr's own Host
stack cannot be used as a stock reference on this hardware; the custom
`stm32_hcd_adapter.c` (or ST's USBH middleware, already proven to fail
identically per Step 19) remain the only Host-mode code paths available
on this SoC/RTOS combination.

Prioritized next steps:

1. **OTG core register diff, Device-mode-working vs. Host-mode-failing**
   (highest value, no new tooling/risk). Dump `GAHBCFG`, `GUSBCFG`,
   `GRXFSIZ`, `DIEPTXF0`/`GNPTXFSIZ` right after the Step 26 Device-mode
   init succeeds, and compare bit-for-bit against what our Host-mode
   init programs (`GRXFSIZ`, `HNPTXFSIZ`, `HPTXFSIZ`, `HCFG`). FIFO RAM
   is a small fixed shared pool on this core — an incorrect RX/TX/
   periodic-TX FIFO split is a classic silent-failure mode that would
   not produce an obvious HAL error, just malformed/truncated transfers
   on the wire, which matches our symptom (STALL/no-response on the
   very first SETUP).
2. **Live SWD single-step through the real SETUP submission.** We have
   a working ST-LINK V3/SWD connection (confirmed this session). Use
   `pyocd`/GDB to halt right at `HAL_HCD_HC_SubmitRequest()` and
   single-step `HCCHAR`/`HCTSIZ`/`HCINT` register writes in real time —
   catches anything a `printf` snapshot could miss (e.g. a write being
   silently ignored or self-clearing before the next log line).
3. **Minimal bare-metal host test, bypassing both our adapter code and
   ST's USBH middleware.** Since both hit the identical STALL (Step
   19), the shared suspect is the HAL/LL HCD layer itself or the
   peripheral-init sequence feeding it. A tiny standalone test — core
   reset, force host mode, power port, wait connect, manually poke one
   control channel, no state machine/retries, full register log at
   every step — isolates the fault to core-init vs. HCD-runtime vs.
   middleware-state-machine.
4. **Targeted re-pass of the H7 errata doc for Host-mode-specific
   entries.** The SWD probe (this session) confirmed exact silicon:
   `Device ID 0x450, Revision ID Rev V`. Re-search
   `docs/es0596-stm32h7rxx7sxx-device-errata-stmicroelectronics.md`
   filtered specifically to Host-mode DMA/FIFO errata (Step 16 only
   found the ID-pin erratum so far).
5. **Enable deeper HAL/USBH debug tracing** if available, for one more
   layer of visibility beyond our own hand-placed log points.

Starting with item 1 next.

## Step 29: OTG core register diff, Device-mode vs. Host-mode (plan item 1)

Executed plan item 1 from Step 28: captured a fair, same-lifecycle-stage
register dump from both modes and diffed them.

### Method

- **Host mode**: added temporary diagnostic `printf()`s to
  `stm32_hcd_adapter.c` — one right after `USB_HostInit()` (dumping
  `GAHBCFG`, `GRXFSIZ`, `HNPTXFSIZ` i.e. `DIEPTXF0_HNPTXFSIZ`,
  `HPTXFSIZ`, `HCFG`), and one right after `HAL_HCD_Start()` inside
  `USBH_LL_Start()` (dumping `GAHBCFG`/`GUSBCFG` again, since the first
  dump happens before the core's global-interrupt-enable step and isn't
  a fair baseline by itself).
- **Device mode**: copied the stock `cdc_acm` sample (from Step 26) to
  a scratch build directory (`/tmp/cdc_acm_diag`, not committed) and
  added one `printk()` right after `usb_enable()` succeeds, reading the
  same OTG_FS core registers directly by address (`0x40080000` base,
  confirmed to match `usbotg_fs` in the board's devicetree). Built and
  flashed exactly like Step 26 (same toolchain env vars).
- Captured serial output over the ST-LINK VCP (`/dev/ttyACM0`) across a
  cold hardware reset for each firmware image.

### Results

Host mode (after `HAL_HCD_Start()`, matching the Device-mode dump's
lifecycle stage — post-enable, before any transfer):
```
GAHBCFG=0x00000001  GUSBCFG=0x20001440
```
Host mode (right after `USB_HostInit()`, one step earlier):
```
GAHBCFG=0x00000000  GRXFSIZ=0x00000200  HNPTXFSIZ=0x01000200
HPTXFSIZ=0x00e00300  HCFG=0x00000200
```

Device mode (right after `usb_enable()`):
```
GAHBCFG=0x00000001  GUSBCFG=0x40001440  GRXFSIZ=0x000000a0
DIEPTXF0=0x001000a0  GCCFG=0x00010000
```

### Analysis

- **`GAHBCFG` matches exactly** (`0x00000001`, i.e. `GINT` enabled,
  everything else identical) once compared at the same lifecycle
  stage. The earlier "0x00000000" Host-mode value was a false lead —
  it was simply captured one step too early (before `HAL_HCD_Start()`
  sets `GINT`), not a real Host-vs-Device discrepancy.
- **`GUSBCFG` differs only in the expected mode-select bit**: Host has
  `FHMOD` (bit 5, `0x20`) set, Device has `FDMOD` (bit 6, `0x40`) set;
  every other bit (`TRDT`, `TOCAL`, etc. — the `0x1440`/`...440` common
  part) is identical. This is exactly what should differ between the
  two modes and confirms no unwanted stray bits in either.
- **FIFO sizing (`GRXFSIZ`/`HNPTXFSIZ`/`HPTXFSIZ`) is set by
  unmodified, stock ST HAL code**, not anything specific to this
  board/port. Confirmed directly from
  `stm32h7xx_ll_usb.c:USB_HostInit()`:
  ```c
  USBx->GRXFSIZ  = 0x200U;
  USBx->DIEPTXF0_HNPTXFSIZ = (((0x100U << 16) & USB_OTG_NPTXFD) | 0x200U);
  USBx->HPTXFSIZ = (((0xE0U << 16) & USB_OTG_HPTXFSIZ_PTXFD) | 0x300U);
  ```
  These three lines are unconditional, hard-coded literals in ST's own
  HAL — every STM32H7 project using this HAL Host-mode driver gets
  these exact same FIFO values, regardless of board or application.
  Since sibling board thread 154603 (Step 24) reports working Host
  mode using presumably this same unmodified HAL, this register
  configuration can be ruled out as the fault: it is proven-working
  elsewhere on the identical silicon/HAL combination.
- Device mode's much smaller `GRXFSIZ`/`DIEPTXF0` (0xa0/0x10 vs.
  0x200/0x100) reflect Zephyr's own device-side FIFO sizing logic
  (endpoint-count/MPS-driven, set by `udc_stm32.c`), not a HAL default
  — not a comparable data point, just confirms each stack sizes FIFOs
  for its own purpose.

### Conclusion for plan item 1

**No discrepancy found at the OTG core/FIFO register level.** Both
modes program `GAHBCFG` identically, `GUSBCFG` identically apart from
the required mode bit, and the Host-mode FIFO partitioning is verified
to be stock, unmodified ST HAL code already proven to work on a sibling
board. This rules out core-level/FIFO misconfiguration as the root
cause and shifts remaining suspicion to runtime behavior *after* this
point — i.e., items 2 and 3 of the Step 28 plan (live single-step
through `HAL_HCD_HC_SubmitRequest()`, or a minimal bare-metal
channel-poke test) are now the most promising next steps, since the
static configuration is clean.

The temporary diagnostic printf's added to `stm32_hcd_adapter.c` for
this comparison are being left in place (low-noise, one-line-per-boot,
useful for any future re-diff) rather than reverted.

## Step 30 — Plan item 2 abandoned (tooling), plan item 3 finds channel stuck in a halt-requested-but-never-completes state

### Plan item 2: live SWD single-step — abandoned, tooling-broken

Attempted live register/step debugging via `pyocd` + `arm-zephyr-eabi-gdb`
to watch `HAL_HCD_HC_SubmitRequest()` execute in real time.

- Generic `pyocd` target `stm32h743xx` (single-core M7-only CMSIS
  pack) misidentifies the H755's actual Cortex-M4 core as "core 0",
  never finding the M7 at all — wrong dual-core topology.
- Installed the correct part-specific pack:
  `pyocd pack install STM32H755ZITx` (`Keil.STM32H7xx_DFP` 4.1.3).
  With `stm32h755zitx` as target and `-M under-reset`, the M7 core's
  ROM table/ETM is now found correctly (`AHB-AP#0`).
- But reading the M7's own System Control Space immediately faults:
  `Error attempting to create component SCS: Memory transfer fault
  (read) @ 0xe000ed78-0xe000ed7b`. GDB RSP register queries (`r7`,
  `xpsr`) then fail with `'register %s not available in this CPU'`.
  Reproduced identically via `pyocd commander` directly (bypassing
  GDB's RSP layer), confirming this is a pyocd/CMSIS-pack core-model
  bug for this exact dual-core topology, not a GDB protocol issue.

**Conclusion: item 2 is not viable with current tooling on this board.**
Would need a different tool (e.g. OpenOCD with a real H745/H755
dual-core config, or ST-LINK-native debugger) to attempt this avenue
again. Not pursued further for now — moved to item 3.

### Plan item 3: bare-metal raw channel test, bypassing ST's USBH middleware

Added `stm32_hcd_adapter_raw_control_test()` (new
`usbhost rawtest` shell command): builds a hand-crafted 8-byte
`GET_DESCRIPTOR(DEVICE)` SETUP packet and drives it directly through
HAL's low-level primitives (`USB_HC_Init()` / `USB_HC_StartXfer()`) on
**host channel 1** — deliberately *not* channel 0, which ST's USBH
middleware's own control pipe already owns and continuously retries via
its background poll thread. Then polls `HCINT`/`HCCHAR`/`HCTSIZ`/
`GINTSTS` every 2 ms for 100 iterations (~200 ms), fully bypassing
`USBH_Process()` and any of ST's stack/state-machine logic.

**Run 1 (shared IRQ left enabled):** channel-1 registers were static for
~38 polls (~76 ms), then *changed on their own* around poll 38–39 with
zero corresponding code of mine touching channel 1. Root cause: the
single shared `STM32_HCD_IRQ` (OTG_FS global interrupt) services *all*
channels, including channel 0's ongoing middleware retries — so
`HAL_HCD_IRQHandler()` was reacting to global/channel-independent
interrupt sources and touching channel 1's state as a side effect,
contaminating the "bare-metal" measurement.

**Fix:** bracket the poll loop with `irq_disable(STM32_HCD_IRQ)` right
after `USB_HC_StartXfer()` and `irq_enable(STM32_HCD_IRQ)` at the end,
so any register change observed is purely hardware, not software/ISR
side effects.

**Run 2 (IRQ disabled around the poll loop) — the key result:**

- `HCINT` (channel 1's own interrupt-status register) stayed
  **`0x00000000` for the entire ~200 ms window** — no `XFRC`
  (transfer complete), no `NAK`, no `STALL`, no `TXERR`, no `CHH`
  (channel halted) — nothing, ever.
- `HCCHAR` read `0xc0100040` from poll 0 onward and never changed.
  Decoded: bit31 `CHENA`=1 **and** bit30 `CHDIS`=1 **simultaneously**.
  `USB_HC_StartXfer()`'s own code explicitly does
  `tmpreg &= ~USB_OTG_HCCHAR_CHDIS; tmpreg |= USB_OTG_HCCHAR_CHENA;`
  right before writing `HCCHAR` — i.e. **HAL itself always clears
  CHDIS and only ever sets CHENA**. It never sets `CHDIS`. So `CHDIS=1`
  being visible at poll 0 (with IRQ disabled immediately after
  `USB_HC_StartXfer()` returns, before any of my code could touch the
  register) means something *else* set that bit in the handful of
  microseconds between the HAL's write and my `irq_disable()` call —
  almost certainly the shared ISR itself, reacting near-instantly to
  some condition on channel 1 (e.g. attempting `USB_HC_Halt()`, which
  sets `CHDIS=1` to request a halt) before I could disable it. This
  also retroactively explains run 1's initial "quiet" polls: the ISR
  had *already* eaten the evidence (by processing whatever fired) even
  before my very first printf.
- Crucially, once `CHDIS=1` is requested, DWC-OTG-family cores are
  expected to complete the in-flight/pending packet (or immediately, if
  nothing is in flight) and then raise a **channel-halted (`CHH`)
  interrupt in `HCINT`**, at which point hardware also clears `CHENA`.
  **None of that happened.** `HCENA` stayed set and `HCINT` stayed
  `0x00000000` for the full 200 ms — the halt request was never
  honored/completed by hardware.
- `HCTSIZ` also never changed (`0x60080008`: `XFRSIZ=8`, `PKTCNT=1`,
  `DPID=SETUP`) — consistent with the transfer descriptor state never
  advancing, i.e. no data ever actually moved.
- `GINTSTS` ticked once, early (~poll 3), from `0x04000021` to
  `0x05000029` and then stayed fixed. Decoded bit diff: bit3 (`SOF`)
  and bit24 (`HPRTINT`, host port interrupt) newly set. So the core's
  frame/SOF timer is alive and a **host port status event** fired once
  — but bit25 (`HCINT`, aggregate host-channels-interrupt) never set,
  confirming channel 1 itself never posted a channel-level interrupt
  to the core, matching the per-channel `HCINT` register reading zero
  throughout.

### Interpretation

This is the clearest signal so far: channel 1 gets armed
(`CHENA=1`), something (the shared ISR, reacting within microseconds)
requests it be halted (`CHDIS=1`), and then the **hardware never
finishes that halt** — no completion interrupt, no NAK, no STALL, no
error, ever, for 200 ms straight. This looks less like "the SETUP
packet was rejected/NAK'd on the bus" and more like **the host channel
state machine itself getting stuck** — armed but never actually
progressing the transaction onto the bus, and not responding to its own
halt request either. This is consistent with (but does not yet prove)
a core-level Host-mode lockup specific to channel/transaction dispatch,
separate from configuration (item 1 already cleared) and separate from
ID/VBUS/pinmux (cleared in earlier steps).

### Gap found while attempting plan item 4 (errata re-review)

`docs/es0596-stm32h7rxx7sxx-device-errata-stmicroelectronics.md`,
the only errata sheet currently checked into `docs/`, is **for the
STM32H7R/H7S family** (RM0477-based, single different die) — **not**
for the STM32H745/H755/H747/H757 dual-core family this board actually
uses. No H745/H755-specific errata sheet (the real one is ST's
ES0392-equivalent for the dual-core H7 line) is present in this repo.
Any earlier errata-based reasoning in this log sourced from that file
should be treated as **not authoritative for this chip** until the
correct errata sheet is obtained and cross-checked. Fetching it was
attempted this session but timed out; obtaining and converting the
correct dual-core H745/755/747/757 errata sheet is now a prerequisite
for a meaningful plan item 4 and is carried forward as an open task.

### Next steps

1. Add `HPRT` (host port status) to the per-poll dump in a follow-up
   run, to see whether the port silently drops "enabled" status during
   the stuck window (would explain a permanently-stuck halt: core
   thinks the port/device is gone and can't complete a halt against a
   port it no longer considers active).
2. Add `HFNUM` (frame number) to the per-poll dump to confirm the
   frame counter is advancing normally during the stall (already
   implied by the one-time `SOF` bit in `GINTSTS`, but not confirmed
   continuously).
3. Obtain the correct STM32H745/H755/H747/H757 errata sheet and
   re-check for any known Host-mode channel-halt/lockup erratum before
   concluding this is a design-level bug vs. a documented silicon
   limitation with a known workaround.

## Step 31 — Bare-metal (no Zephyr, no RTOS) A/B test does NOT reproduce the stuck-channel symptom → bug is Zephyr-specific, not hardware/errata

Followed through on plan item 3's logical conclusion: built a
completely independent, minimal bare-metal firmware using ST's own
unmodified STM32CubeH7 HAL (no Zephyr, no USBH middleware, no RTOS,
no shared/threaded IRQ handling) that performs the *exact* same raw
`HAL_HCD_HC_Init()` / `USB_HC_StartXfer()` control-transfer probe on
host channel 1 that the Zephyr rawtest used in Step 30, on the same
physical board/connector/cable/camera.

### Build

- Based on ST's vendored `STM32CubeH7` GPIO_Demo example for
  NUCLEO-H745ZI-Q (closest family match; H745/H755 share package and
  pinout, differ mainly in TrustZone/security — irrelevant here).
- CM7-only application logic; CM4 required ST's stock prebuilt
  `GPIO_Demo_CM4.elf` (harmless blink app) flashed to bank 2
  (`0x08100000`) plus the option byte `BCM4=1` (CM4 auto-boot enabl
  ed) and the standard HSEM release handshake in `main()` — **without
  this, D2 domain (which hosts `USART3` and `USB_OTG_FS`) never wakes
  and CM7 accesses to those peripherals hang silently.** (See "Gotchas"
  below — this cost the most debugging time.)
- Clock/pin setup deliberately mirrors the already-proven-working
  Zephyr configuration: `USB_OTG_FS` kernel clock from **HSI48**
  (matching the board's devicetree `STM32_SRC_HSI48`/`USB_SEL(3)`),
  `USART3` on PD8/PD9 at 115200 baud (matching Zephyr's console UART),
  PA11/PA12 AF10 (`OTG1_FS`) for D-/D+, PD10 active-low VBUS enable.
- Built with the Zephyr SDK's `arm-zephyr-eabi-gcc` (no ARM
  `nano.specs`/`nosys.specs` available in that SDK — linked with
  custom minimal newlib syscall stubs instead).
- `HCD_HandleTypeDef.Init` mirrors the Zephyr adapter's field values
  (`Host_channels=8`, `HCD_SPEED_FULL`, `HCD_PHY_EMBEDDED`,
  `Sof_enable=ENABLE`, DMA/low-power/VBUS-sensing all disabled).

### Gotchas hit while bringing this up (for future reference)

- **`USBx_HPRT0`/`USBx_HC(i)`/`USBx_HOST` macros require locally
  declared `USBx`/`USBx_BASE` variables** — they are not globally
  defined constants; ST's own HAL functions declare
  `uint32_t USBx_BASE = (uint32_t)USBx;` at the top of each function
  body. Any external code using these macros must do the same.
- The FS USB peripheral's clock-enable macro on dual-core H7 parts is
  `__HAL_RCC_USB2_OTG_FS_CLK_ENABLE()` — despite the "USB2" in the
  name, this is the peripheral mapped to `USB_OTG_FS`/PA11-12 ("USB1"
  in ST's naming refers to `USB_OTG_HS`).
- **The dual-core boot handshake is not optional**, even for a
  CM7-only test that never touches CM4 functionally: CM7's access to
  any AHB1/D2-domain peripheral (`USART3`, `USB_OTG_FS`) blocks
  forever unless (a) CM4 has actual (even trivial) code to run, (b)
  the `BCM4` option byte enables CM4 auto-boot at reset, and (c) CM7
  performs the standard `HAL_HSEM_FastTake`/`HAL_HSEM_Release`
  handshake after `SystemClock_Config()` to wake D2 domain. Skipping
  any one of these three causes a silent hang with a truncated/no
  UART print — this was misdiagnosed twice before finding the actual
  root cause below.
- **The actual root cause of persistent, exact, byte-identical
  truncated UART output** (always exactly 12 bytes: `"\r\n\r\nbare-met"`)
  turned out to be unrelated to CM4/D2 domain at all: the hand-written
  `Makefile` never compiled/linked `stm32h7xx_it.c`, so `SysTick_Handler`
  (and `HardFault_Handler`, etc.) remained the **weak, default
  infinite-loop stub** from the startup file instead of the real
  `HAL_IncTick()`-calling handler. The very first SysTick interrupt
  after `HAL_Init()` vectored into that infinite loop and the CPU never
  returned — confirmed conclusively via `STM32_Programmer_CLI -c
  port=SWD mode=HOTPLUG -halt -coreReg PC LR MSP PSP XPSR`, which
  showed `PC` frozen at the `ADC3_IRQHandler`/default-handler infinite
  self-branch, `LR` an `EXC_RETURN` value, and `XPSR.IPSR=15` (the
  SysTick exception number) — i.e., **stuck forever inside the SysTick
  exception**, not in application code. (Note: querying core state
  with the default `mode=UR`/software-reset connect silently resets
  the chip on every invocation and always reports the reset vector —
  useless for live-hang inspection; `mode=HOTPLUG` avoids the reset
  and reports the true live PC/registers. This is a much more reliable
  live-inspection technique than the previously-abandoned pyocd/OpenOCD
  route, which still fails outright on this dual-core part's CoreSight
  topology.) Adding `stm32h7xx_it.c` to the Makefile's source list
  fixed it immediately.

### Result

With the fix applied, the bare-metal firmware boots cleanly and the
channel-1 probe **completes on the very first poll iteration**:

```
bare-metal usbh rawtest: boot OK
HAL_HCD_Init OK
HAL_HCD_Start OK
HPRT after VBUS+200ms = 0x00021403 (PCSTS=1 PENA=0)
HPRT after port reset = 0x0002140f
HAL_HCD_HC_Init(ch=1) -> 0
before StartXfer HCCHAR=0x00100040 HCTSIZ=0x00000000 HCINT=0x00000000 GINTSTS=0x05000029
USB_HC_StartXfer -> 0
poll[00] HCINT=0x00000021 HCCHAR=0x20100040 HCTSIZ=0x20000008 HFNUM=0x2e75001b HPRT=0x0002140f GINTSTS=0x07000029
HCINT went non-zero after 0 polls, stopping
done - halting
```

`HCINT=0x00000021` = `XFRC` (bit 0, transfer completed) | `ACK`
(bit 5) — the control-transfer SETUP transaction completed normally
and was ACKed, on the **very first poll**, with **no stall, no
lockup, and no CHENA/CHDIS contradiction**. `HPRT` shows the port
enabled/connected (`PCSTS=1`) throughout, and `HFNUM` shows a live,
advancing frame counter (`0x2e75001b`), confirming the core's SOF/frame
timer and USB clock are healthy.

**This is the opposite of the Zephyr rawtest's result** (Step 30):
under Zephyr, `HCCHAR` showed a contradictory simultaneous
`CHENA=1`/`CHDIS=1`, and `HCINT` stayed stuck at `0x00000000` for the
entire ~200 ms poll window with zero completions of any kind.

### Conclusion

**The USB Host channel lockup is not a hardware defect, not a silicon
errata, and not a fundamental limitation of this board's `USB_OTG_FS`
peripheral.** Identical hardware, identical pins, identical clock
source (HSI48), identical camera, identical cable, identical
`HAL_HCD_HC_Init()`/`USB_HC_StartXfer()` call sequence — the only
variable that changed was the software stack around it (bare-metal
ST HAL vs. Zephyr's `stm32_hcd_adapter.c` + USBH driver + RTOS
scheduling/IRQ handling) — and the bug disappears entirely on
bare-metal. **The root cause is isolated to the Zephyr integration
layer**, not the STM32H755 host-mode hardware itself. This rules out
plan item 4 (errata) as the explanation and redirects further work
squarely at the Zephyr HCD adapter / USBH driver interaction: likely
candidates are IRQ handling/priority/masking differences, a missed or
double register write in the adapter (e.g. something writing `CHDIS`
that HAL's own `USB_HC_StartXfer()` never does — noted but unexplained
in Step 30), or a race between Zephyr's channel-arm path and its ISR.

### Next steps

1. Line-by-line diff `stm32_hcd_adapter.c`'s channel start/IRQ path
   against ST's stock `stm32h7xx_hal_hcd.c`/`stm32h7xx_ll_usb.c` used
   here unmodified, focusing on anything that could set `CHDIS`
   without `CHENA` ever completing, or that masks/misroutes the
   `OTG_FS` global interrupt before the channel-halt/completion
   interrupt is serviced.
2. Re-run the Zephyr rawtest with the Zephyr IRQ handler temporarily
   patched to log entry/exit and raw `HCINT`/`HCCHAR` on every
   invocation, to see whether the ISR ever runs at all during the
   stuck window (if it never runs, the bug is upstream — NVIC
   config/masking; if it runs but doesn't clear the right bits, the
   bug is in the adapter's ISR body itself).
3. Once fixed, re-run this exact bare-metal test as a regression
   baseline/reference alongside the corrected Zephyr driver to confirm
   parity.
4. Errata sheet mismatch (STM32H7R/H7S vs. real H745/755/747/757) noted
   in Step 30 remains an open documentation gap, but is now
   deprioritized given the bare-metal result already rules out a
   hardware/errata-level explanation.

## Step 32 — Bit-for-bit clock/register match still fails; port-reset gap found, but "electrical" theory falsified by the bare-metal precedent

### Goal

Cross-analyze the clock/register dumps captured from the Zephyr
rawtest against the bare-metal reference (Step 31) to find the actual
software difference behind the stuck `CHENA+CHDIS`/`HCINT=0` channel
symptom, per the user's suggestion that pinmux/clock differences were
worth checking.

### Clock tree: 480 MHz vs 400 MHz ruled out

The Zephyr board's devicetree (`nucleo_h755zi_q_stm32h755xx_m7.dts`)
configures `PLL1` (`M=1, N=120, P=2`) from the 8 MHz HSE bypass for a
**480 MHz** `SYSCLK` (`HCLK=240 MHz`, `PCLK1/2=120 MHz`), requiring
`VOS0` (over-drive) — a genuinely different, deliberate clock config
from the bare-metal test's conservative 400 MHz (`VOS1`) config
inherited from ST's `GPIO_Demo` template.

Reconfigured the bare-metal test's `SystemClock_Config()` to the exact
same `PLL1`/`VOS0`/480 MHz tree as the Zephyr board and re-ran it:

```
bare-metal usbh rawtest: boot OK
clocks SYSCLK=480000000 HCLK=240000000 PCLK1=120000000 PCLK2=120000000
HAL_HCD_Init OK
HAL_HCD_Start OK
HPRT after VBUS+200ms = 0x00021403 (PCSTS=1 PENA=0)
HPRT after port reset = 0x0002140f
HAL_HCD_HC_Init(ch=1) -> 0
USB_HC_StartXfer -> 0
poll[00] HCINT=0x00000021 HCCHAR=0x20100040 ...
HCINT went non-zero after 0 polls, stopping
```

**Bare-metal still succeeds instantly at 480 MHz** — same clean
`HCINT=0x21` (XFRC+ACK) on poll 0. The 480 MHz vs 400 MHz clock
difference is **ruled out**.

### Static register dump: identical on both sides

Added the same `GAHBCFG`/`GUSBCFG`/`GCCFG`/`GRXFSIZ`/`HNPTXFSIZ`/
`HPTXFSIZ`/`HCFG` dump to bare-metal (now at matching 480 MHz) and
compared directly against Zephyr's rawtest dump:

| Register | Bare-metal (480 MHz) | Zephyr rawtest |
|---|---|---|
| GAHBCFG | `0x00000001` | `0x00000001` |
| GUSBCFG | `0x20001440` | `0x20001440` |
| GCCFG | `0x00010000` | `0x00010000` |
| GRXFSIZ | `0x00000200` | `0x00000200` |
| HNPTXFSIZ | `0x01000200` | `0x01000200` |
| HPTXFSIZ | `0x00e00300` | `0x00e00300` |
| HCFG | `0x00000200`/`0x00000201`* | `0x00000200` |

*(HCFG differs only because it's read before vs. after `USB_HC_Init()`
sets the speed field on Zephyr's side in one capture — not a real
config difference; the `HCD_HandleTypeDef.Init` struct fields
`Host_channels/speed/dma_enable/phy_itface/Sof_enable/
low_power_enable/vbus_sensing_enable/use_external_vbus` were confirmed
identical by direct source comparison.)

**Every static register and clock value now matches bit-for-bit.**
This rules out clock config, FIFO sizing, and core mode/PHY selection
as the difference.

### `HAL_PWREx_EnableUSBVoltageDetector()` and PA9/PA10 GPIO config ruled out

Checked two more candidate differences from `adapter_init_hardware()`
that bare-metal's `main.c` never does at all: the USB 3.3V voltage
detector wait (`HAL_PWREx_EnableUSBVoltageDetector()`/
`PWR_FLAG_USB33RDY`) and PA9 (VBUS sense input)/PA10 (ID pin, AF10)
GPIO configuration. Bare-metal succeeds with neither of these present,
so their absence in bare-metal (and presence in Zephyr) cannot be
the cause of Zephyr's failure — ruled out.

### The real gap found: rawtest was missing an explicit port reset

Dumping `HPRT` at the very start of the Zephyr rawtest (before this
session's fix) showed `PCSTS=1` but **`PENA=0`** — the port was
*connected* but never *enabled*, because the rawtest command assumed
the background USBH middleware (or the shell's earlier `usbhost arm`/
`descriptor` flows) had already reset/enabled the port. Bare-metal, in
contrast, always performs an explicit port reset (`HPRT.PRST`
pulse + settle delays) immediately before starting any channel
transfer. Added the same explicit `HAL_HCD_ResetPort()` call (guarded
on `PENA==0`) to `stm32_hcd_adapter_raw_control_test()`:

```c
if (((hprt >> 2) & 0x1U) == 0U) {
    printf("rawtest: HPRT.PENA=0 (0x%08lx), forcing port reset\n", ...);
    (void)HAL_HCD_ResetPort(&hcd_handle);
    k_msleep(50);
    ...
}
```

After this fix, `HPRT` correctly shows `PENA=1` before the transfer,
and `HCINT` **does now update** instead of staying frozen at `0`:

```
rawtest: HPRT.PENA=0 (0x00021401), forcing port reset
rawtest: HPRT after reset = 0x00021405
...
rawtest: poll[00] HCINT=0x00000080 HCCHAR=0xc0100040 ...
rawtest: HCINT went non-zero after 0 polls, stopping
```

`HCINT=0x00000080` decodes to **`TXERR`** (bit 7, Transaction Error) —
a real, hardware-reported "no response from device" condition, not a
stuck/frozen register. This is progress: the previous "CHENA+CHDIS
both set, HCINT stuck at 0 forever" symptom in the rawtest was masking
a real transaction error underneath a missing-port-reset bug in the
test harness itself. With the harness fixed, the rawtest's result now
matches what the *production* USBH middleware path independently
reports on channel 0 in the same session (`HCINT=0x00000000`,
`HC_state=HC_HALTED (2)`, repeated `"unrecovered error"` /
`"Control error: Device not responding"` messages, cycling through
disconnect/reconnect/reset without ever completing enumeration).

### Initial (retracted) hypothesis: electrical/VBUS

TXERR was initially (incorrectly) attributed to a physical/power-layer
cause (e.g. VBUS sag under the camera's inrush/current draw), since
`HPRT`/`PA9`/`PG7 (nFAULT)` all looked electrically normal
(`PCSTS=1`, `PENA=1`, no over-current fault) and all *static*
registers matched the working bare-metal reference exactly.

**This hypothesis was falsified by the user, correctly pointing out
that the bare-metal test used the exact same board, same `USB_OTG_FS`
peripheral, same CN13 connector, same cable, and the same physical
camera — and got a clean `HCINT=0x21` (XFRC+ACK) success on the very
first SETUP transaction (Step 31 and its 480 MHz re-run above).** If
the camera's power draw or the board's VBUS supply were physically
inadequate, bare-metal would have failed identically. It didn't. The
cause must still be dynamic/software or device-state related, not
electrical capability.

### Current leading theory: device wedged by many hours of aborted resets, needs a real VBUS power-cycle

The camera has been through many hours of repeated incomplete
enumeration attempts across this entire debugging session (`ST USBH`
disconnect/reconnect/reset loops, `usbhost rawtest` runs, etc.) without
ever having its `VBUS` actually removed — `USBH_LL_DriverVBUS()`
control (`PD10`) was left continuously asserted throughout. It is
plausible the camera's own USB controller has wedged into a
non-responsive state that only a genuine power-off/on (not a mere
D+/D- line reset, which is all `HAL_HCD_ResetPort()`/`USB_ResetPort()`
does) would clear.

Added a new, software-only diagnostic to test this without requiring
any physical hardware action:

- `stm32_hcd_adapter_vbus_cycle(uint32_t hold_ms)` in
  `stm32_hcd_adapter.c`: forces `PD10` high (VBUS off, active-low) for
  `hold_ms`, then low again (VBUS on) with a 200 ms inrush/ramp settle
  delay, logging `PD10`/`PA9`/`HPRT` at each step.
- `usbhost vbuscycle` shell command in `usb_host_shell.c`: pauses the
  middleware poll thread, calls the above with a 2-second hold, then
  resumes polling.

### Status / next steps

1. Flash the firmware with `usbhost vbuscycle` added (in progress as
   of this writing).
2. Run `usbhost vbuscycle` to force a genuine VBUS power-off/on for the
   camera, then `usbhost rawtest` again to see if `HCINT` finally shows
   a clean completion instead of `TXERR`, mirroring bare-metal's
   result.
3. If the power-cycle theory is confirmed, the true "production" fix
   is likely: (a) always power-cycle VBUS on `USBH_LL_Connect()`/
   enumeration-retry paths instead of only resetting the port, or (b)
   diagnose why this specific device needs a longer post-power-on
   settle time before responding to SETUP than what
   `USBH_LL_DriverVBUS()`'s existing 100 ms delay provides.
4. If the power-cycle does *not* fix it, the remaining candidates are
   narrower still: dynamic IRQ/NVIC timing differences between
   bare-metal's polling-only approach and Zephyr's interrupt-driven
   `HAL_HCD_IRQHandler()` path, or USBH middleware's own channel/URB
   state machine issuing register writes (e.g. `CHDIS`) concurrently
   with the rawtest/production channel in ways not yet fully traced.
5. Camera physically remained plugged into CN13 throughout this
   entire step, matching all prior steps back to Step 1 — no cable or
   connector was changed.

## Step 33 — vbuscycle result (falsified), channel-0-collision theory (falsified), Ethernet/EMI theory (falsified), reset-timing double-checked (not the cause), bare-metal reconfirmed to still succeed *right now*

Ran the plan from Step 32:

1. **`usbhost vbuscycle` (real VBUS power-off/on)** — confirmed genuine
   power removal via `PA9` going `0` then `1`. Ran `usbhost rawtest`
   and the production middleware immediately afterward: **both still
   failed identically** (`HCINT=0x80` TXERR in rawtest; production's
   `HC_state=2`/"unrecovered error" loop unchanged). **The
   "wedged-by-history-of-failed-attempts" theory is falsified** — a
   genuine electrical power cycle of the device does not help.

2. **Channel-0-collision theory** ("production's own channel 0 might
   still be armed/`CHENA=1` and colliding on the shared D+/D- lines
   with the rawtest's channel 1"): added instrumentation dumping
   `HCCHAR(ch0)` before/after an explicit `HAL_HCD_HC_Halt()` call at
   the top of `stm32_hcd_adapter_raw_control_test()`. Rebuilt, reflashed,
   ran `rawtest`: `HCCHAR(ch0)` read `0x00000000` (or, in a later run,
   `0x20100040` with `CHENA` bit **clear** either way) both before and
   after the halt call — **channel 0 was never actually armed**, so
   halting it was a no-op. **This theory is falsified.** `HCINT=0x80`
   TXERR persisted regardless.

3. **Reset-timing double-check** (per explicit user request — "maybe
   timing of signal is important as part of reset"): compared the
   exact reset pulse widths.
   - Bare-metal (`/tmp/usbh_bare_test/main.c`): manual `HPRT.PRST`
     assert for `HAL_Delay(20)`, deassert for `HAL_Delay(20)` — 40 ms
     total.
   - Zephyr rawtest's fix calls `HAL_HCD_ResetPort()` →
     `USB_ResetPort()` in ST's HAL, which asserts `PRST` for
     `HAL_Delay(100)` and deasserts for `HAL_Delay(10)` (110 ms), plus
     an additional `k_msleep(50)` added around it — **160 ms total**,
     i.e. **4x longer and more spec-conservative** than bare-metal's
     sequence, not shorter. Port state after reset
     (`HPRT=0x00021405`: `PCSTS=1, PENA=1, PSPD=1` Full Speed,
     `PPWR=1`) is electrically identical to bare-metal's own
     post-reset state. **Reset timing is ruled out** — if anything
     Zephyr's is safer than the sequence that's proven to work.

4. **Ethernet/EMI-contention theory** (Zephyr runs the Ethernet
   MAC/PHY, netif, and SysTick concurrently; bare-metal runs nothing
   else, so physically-adjacent-pin crosstalk during Ethernet TX/RX
   could theoretically corrupt the SETUP transaction): ran
   `net iface down 1` to fully quiesce the Ethernet interface, then
   `usbhost rawtest` again. **Result unchanged**: `HCINT=0x80` TXERR
   on the very first poll. **This theory is falsified.**

5. **USB-device-stack-conflict theory**: the board's stock devicetree
   labels the OTG_FS peripheral node `zephyr_udc0` (Zephyr's naming
   convention for a USB *device* controller) with
   `pinctrl-0 = <&usb_otg_fs_dm_pa11 &usb_otg_fs_dp_pa12>` and
   `status = "okay"`, raising the concern that Zephyr's own USB device
   stack might be silently initialized in parallel with our raw-HAL
   host code, fighting over the same peripheral/pins. Checked the
   actual build's generated `.config`
   (`/tmp/stm32h7-zephyr-hello-shell/zephyr/.config`): **no
   `CONFIG_USB_DEVICE_STACK`, no `CONFIG_UDC_*` symbols are set at
   all** — the device-mode driver is not compiled in, so there is no
   competing UDC instance. **This theory is falsified.**

6. **Critical control re-run**: re-flashed the *exact* bare-metal
   `.bin` (unchanged since Step 31/32, timestamp confirmed to be from
   earlier this same session) with the *same physical camera, cable,
   and connector* that has just been failing under Zephyr for hours,
   right now, live:
   ```
   HPRT after VBUS+200ms = 0x00021403 (PCSTS=1 PENA=0)
   HPRT after port reset = 0x0002140f
   poll[00] HCINT=0x00000021 HCCHAR=0x20100040 HCTSIZ=0x20000008 HFNUM=0x20d8001b HPRT=0x0002140f
   HCINT went non-zero after 0 polls, stopping
   ```
   **`HCINT=0x21` = `XFRC`+`ACK` — clean success, instantly, on the
   first attempt.** This conclusively rules out camera/cable/connector
   degradation from hours of repeated failed enumeration attempts as
   an explanation: the identical physical setup works perfectly under
   bare-metal moments before/after failing identically under Zephyr.

### Status after Step 33

Every concrete, testable hypothesis for *why* Zephyr's USB host
transaction fails while bare-metal's identical transaction succeeds
has now been individually tested and falsified: clock tree, static
core/FIFO registers, PWR/GPIO config, port-reset timing, VBUS
electrical capability, "wedged device" history, channel-0 bus
collision, Ethernet/EMI contention, and USB-device-stack conflict are
all ruled out by direct measurement. The paradox remains: same board,
same connector, same cable, same camera, same registers, same reset
sequence, tested minutes apart — bare-metal succeeds every time,
Zephyr's `HCINT` reports genuine hardware-level `TXERR` every time.

Remaining untested candidates, roughly in order of plausibility:
1. **Interrupt/NVIC-driven timing jitter**: even with
   `irq_disable(STM32_HCD_IRQ)` in the rawtest, Zephyr's SysTick tick
   and other enabled interrupts (I2C, tick, etc.) are still live and
   could delay the CPU's response inside the tight polling window
   right after `USB_HC_StartXfer()`, in a way a bare-metal busy-loop
   with fewer active peripherals never experiences. Untested because
   it requires either disabling all other interrupts too (a coarse
   test) or genuine cycle-accurate instrumentation.
2. **A byte-level SETUP-packet or FIFO-write corruption** from
   cache/MPU differences between bare-metal's and Zephyr's default
   memory attributes — considered but currently judged unlikely for
   this specific 8-byte `static const`-like buffer, since it is
   written once at boot (from the `.data` load) and never mutated at
   runtime, so there is no write-then-stale-read window; would need
   an actual bit-level capture of the D+/D- line during the failing
   Zephyr SETUP token to confirm or rule out.
3. **Direct electrical capture (software logic-analyzer style GPIO
   sampling) of the D+/D- lines** during both the succeeding
   bare-metal SETUP and the failing Zephyr SETUP, to see if the
   physical waveform itself differs (voltage levels, edge timing,
   glitches) — this is the most conclusive remaining option since
   every software/register-level avenue has been exhausted without
   result.

### Note: Zephyr has no upstream USB Host (UHC) driver for STM32/DWC2 at all

Prompted by ST's "Zephyr ecosystem support" marketing page, checked
whether the vendored Zephyr tree
(`vendor/zephyrproject/zephyr/drivers/usb/uhc/`) has *any* native
host-controller driver for STM32's OTG_FS/HS (Synopsys DesignWare
Core 2, "DWC2") peripheral. It does not:

- `drivers/usb/uhc/` only contains `uhc_virtual.c`, the NXP
  `uhc_mcux_{ip3516hs,khci,ehci,ohci}.c` drivers, and
  `uhc_max3421e.c` (an external USB-host-over-SPI chip). **No
  `uhc_dwc2.c` exists anywhere in the tree.**
- The only DWC2-related driver present is `drivers/usb/udc/udc_dwc2.c`
  — **device-mode (UDC) only** — plus its devicetree bindings
  (`snps,dwc2.yaml`, `st,stm32f4-fsotg.yaml`), which likewise only back
  the device-mode driver.

**Conclusion**: ST's "full Zephyr support" claim covers device-mode
USB (and other peripherals), not USB Host on this SoC family — Zephyr
upstream has zero native USB Host support for any STM32 OTG DWC2 core,
so there is no alternate/official code path we could or should be
using instead. This project's `stm32_hcd_adapter.c` (raw ST HAL
`HCD_*` calls + ST's USBH middleware, entirely bypassing Zephyr's
`subsys/usb/host`/UHC framework) is not a workaround around some other
working Zephyr subsystem — it *is* the only USB host stack that exists
for this hardware in this ecosystem today. This also confirms there is
no hidden competing Zephyr USB-host driver silently contending for the
OTG_FS peripheral: the TXERR bug is entirely contained within this
project's own custom glue/ST middleware, not an upstream Zephyr defect.

## Step 34 — D+/D- confirmed electrically silent; SOF/frame-timer confirmed genuinely dead under Zephyr (not a register misconfiguration)

Continuing from Step 33, added a software D+/D- GPIO capture tool
(`usb_gpio_capture.c/.h`, a tight DWT-cycle-counted `GPIOA->IDR`
sampler) already wired into `USBH_LL_SubmitURB()` and into the
`rawtest` shell command.

- **Production path capture** (`usbhost arm` + `usbhost resume`):
  32768 samples over ~1.08ms during a real failing SETUP submission —
  **zero transitions**, D+/D- both stuck at 0 (SE0) the entire window.
- **Isolated `rawtest` capture** (bypasses USBH middleware entirely,
  bare HAL `HC_Init`/`StartXfer`): same result — **zero transitions**,
  even though `HCINT` sometimes reports `0x21` (success!) instead of
  the usual `0x80` (TXERR). So the bus never visibly toggles either
  way, success or failure.
- Added a `usbhost gpio` shell command dumping live
  `GPIOA->MODER/AFR[1]/OTYPER/OSPEEDR/PUPDR/IDR` for PA11(D-)/PA12(D+):
  **pinmux is bit-for-bit correct** (AF10, push-pull, very-high speed,
  no pull) — ruled out.
- Added RCC diagnostics: `HSI48ON=1 HSI48RDY=1`,
  `D2CCIP2R.USBSEL=3` (=HSI48, correctly selected — initially
  misread as "disabled" because `0b11` looks like "all bits set"; the
  LL header defines `HSI48=0b11`, `DISABLE=0b00` — corrected), and
  `AHB1ENR.USB2OTGHSEN=1`. **Clock source config is provably correct.**

### FRNUM/SOF timing anomaly discovered, then run to ground

- `HFNUM` sampled over a 20×1ms loop: bare-metal's `FRNUM` (low 16
  bits, increments once per SOF) changed **20/20 times** — perfect 1ms
  cadence. Zephyr's `FRNUM` changed only **0–1 times in the same
  20ms** — i.e. SOF is essentially not firing.
- `HFIR` (expected interval register) differs: bare-metal reads back
  the **silicon reset default `0x0000EA60` (60000)**, Zephyr reads
  `0x0000BB80` (48000, the value ST's HAL's
  `USB_InitFSLSPClkSel(HCFG_48_MHZ)` writes on the port-enable-changed
  interrupt path). This looked like a smoking gun (48000 cycles at a
  slower-than-expected effective clock would produce a slow SOF), but:
  - **Forcing `HFIR` back to `60000` in Zephyr's `rawtest` did NOT
    restore normal cadence** — a re-swept FRNUM immediately after the
    forced write still showed **0 changes in 20ms**. This falsifies
    "wrong HFIR value" as the cause; it's not a frame-interval
    miscalculation, the frame timer just isn't ticking at all.
  - Read `PCGCCTL` (Power/Clock Gating Control, offset `0xE00`): `0`
    in both builds (`STPPCLK`/`GATEHCLK` both clear) — **the PHY/AHB
    clock gate is not disabling anything; ruled out.**
- **Definitive, register-read-artifact-proof test**: instead of
  reading `HFNUM`/`FRNUM` (a data register), directly polled the raw
  `GINTSTS.SOF` **interrupt status flag** (write-1-to-clear), clearing
  it and counting how many times it re-asserts itself over a fixed
  ~5ms busy-poll window, on both builds:
  - **Bare-metal: reasserted 32 times in ~5ms** — SOF is genuinely,
    repeatedly firing.
  - **Zephyr: reasserted 0 times in the same ~5ms window** — SOF is
    **not firing at all**, full stop. This is not a decode or
    register-alignment issue (same struct/offsets work fine for
    `PCGCCTL`/`HFIR`); the raw hardware interrupt-pending bit for SOF
    simply never sets under Zephyr.

### What this rules in/out

All host-mode configuration registers touched by the driver
(`GAHBCFG=0x00000001`, `GUSBCFG=0x20001440`, `GCCFG=0x00010000`,
`PCGCCTL=0x0`, `HFIR` after forcing to match) are **identical or
equivalent** between the working bare-metal reference and the failing
Zephyr build. The one register that differs (`HCFG.FSLSPCS`: bare-metal
reads back `00` — its unconfigured reset default, because bare-metal's
build never processes the HPRT `PENCHNG` interrupt path that would
normally rewrite it — vs Zephyr's `01`/48MHz) was tested directly by
forcing values and does **not** explain the SOF freeze either.

This means the root cause is **not a static register misconfiguration
at all** — every register we can read says "this should work." The
OTG core's internal SOF/frame-timer state machine itself is not
running under Zephyr, which is consistent with (and directly explains)
the earlier D+/D- zero-activity finding, since the core's internal
non-periodic-transfer scheduler is gated by the SOF/frame timer.

### Attempted follow-up test (not completed): AHB bus contention from Ethernet

Hypothesized that a concurrently-active peripheral (Ethernet MAC/DMA,
the only significant other AHB bus master active in this app that
bare-metal doesn't have) might be stalling the OTG core's internal
timer via bus arbitration contention. Attempted to disable
`CONFIG_NETWORKING`/`CONFIG_NET_L2_ETHERNET` in `prj.conf` to test this
in isolation, but **the app has a hard compile-time dependency on
networking** (`http_server.c`'s `HTTP_SERVICE_DEFINE` and
`CONFIG_NET_CONFIG_PEER_IPV4_ADDR` fail to build without it) — this
would require non-trivial `#ifdef`-gating of `http_server.c` to test
cleanly, so it was reverted rather than restructuring the app. This
remains an open, untested hypothesis for a future session.

### Updated conclusion / next-step candidates

- The bug is a genuine **hardware state-machine problem** (SOF
  generator not running), not a value programmed into any
  register we've been able to inspect so far.
- Candidates not yet tested:
  1. AHB bus contention from Ethernet (see above — needs
     `http_server.c` refactor to test cleanly).
  2. Some other Zephyr-boot-time interaction with the OTG core
     *before* our own `HAL_HCD_Init()` runs (e.g., a stray IRQ, a
     partial/previous init attempt, or a shared-resource conflict from
     another driver touching AHB1ENR/D2CCIP2R after our code sets it)
     that leaves the core in a state from which `USB_CoreReset()`
     (which does correctly wait for `GRSTCTL.AHBIDL` and poll
     `GRSTCTL.CSRST` self-clear — verified identical in both builds)
     doesn't fully recover.
  3. Actual measured HSI48 oscillator frequency/trim under Zephyr vs
     bare-metal (both report `HSI48RDY=1`, but an out-of-spec real
     frequency, e.g. due to some calibration difference, has not been
     independently measured with an external reference).
- All changes this step are additive diagnostics in
  `stm32_hcd_adapter.c`'s `rawtest` path (HFIR-force experiment,
  `PCGCCTL` read, `GINTSTS.SOF`-reassert counter) and the equivalent
  instrumentation in the scratch bare-metal reference
  (`/tmp/usbh_bare_test/main.c`, not in git). No production-path logic
  was changed.

## Step 35 — CORRECTION: the "SOF is dead" conclusion in Step 34 was a self-inflicted measurement artifact (ISR racing our own poll), not a real hardware bug

Continuing from Step 34, ran two more targeted A/B experiments before
accepting the SOF-dead theory as final:

1. **Ethernet AHB-clock-gating test**: gated off
   `RCC->AHB1ENR.{ETH1MACEN,ETH1TXEN,ETH1RXEN}` for the duration of the
   `GINTSTS.SOF`-reassert probe, then restored them. Result: still 0
   reasserts in ~5ms with Ethernet clocks off. **Falsifies AHB bus
   contention from the Ethernet MAC as the cause.**
2. **CM4 auto-boot test**: discovered via a new `FLASH->OPTSR_CUR`
   read that this board's **BCM4 option byte is set (`BCM4=1`)** — the
   second Cortex-M4 core auto-boots at every reset — and its flash
   bank at `0x08100000` contains a real (non-erased) firmware image
   (valid vector table, likely left over from an earlier session's
   `nucleo_h755zi_q/stm32h755xx/m4` experiment per this repo's own
   documented dual-core target support). Temporarily programmed
   `BCM4=0` via `STM32_Programmer_CLI -ob BCM4=0`, hard-reset, and
   re-ran the probe: **still 0 SOF reasserts in ~5ms with CM4 held in
   reset.** Falsifies CM4/dual-core contention as the cause.
   **Restored `BCM4=1`** afterward (this board's original
   configuration; not the actual bug, so left as found).

Both of these came back negative, which is when the real bug in *our
own test methodology* was found:

### The actual explanation: our own enabled ISR was clearing GINTSTS.SOF before our busy-poll loop could read it

`stm32_hcd_adapter.c`'s production init path registers and enables the
OTG_FS peripheral interrupt (`IRQ_CONNECT(STM32_HCD_IRQ, ...)` +
`irq_enable(STM32_HCD_IRQ)`), and `GINTMSK.SOFM` is unmasked
(confirmed: `GINTMSK=0xa3200018`, bit3 set). `HAL_HCD_IRQHandler()`
services `GINTSTS.SOF` on every real SOF event and clears it in the
ISR. Our `rawtest` diagnostic was a **plain busy-poll loop reading the
same flag from ordinary (non-ISR) code** — if the hardware SOF event
is real and the ISR is enabled, the ISR (higher priority, essentially
zero latency) will always win the race and clear the flag before our
polling loop's next read, making the flag look permanently clear from
our vantage point *even though SOF is firing normally*. Bare-metal
never registers/enables any NVIC IRQ for the OTG core at all (pure
super-loop polling design), so nothing ever intercepted the flag
there — which is exactly why bare-metal's identical probe legitimately
saw 32 reasserts in 5ms and Zephyr's saw 0.

**Confirming experiment**: added `irq_disable(STM32_HCD_IRQ)` right
before the FRNUM/SOF diagnostic block in `rawtest`, ran the identical
`GINTSTS.SOF`-reassert probe with the ISR now unable to steal the flag,
then `irq_enable(STM32_HCD_IRQ)` again afterward. Result: **7-8
reasserts in ~5ms** — consistent with a real, correctly-running ~1ms
SOF cadence, once our own interrupt handler was no longer racing us
for the same flag.

### This retracts Step 34's headline conclusion

The OTG core's SOF/frame-timer generation is **not** actually dead or
misconfigured under Zephyr; the entire HFIR/FRNUM/SOF-dead narrative
in Step 34 was chasing a self-inflicted artifact of testing a
W1C interrupt flag from foreground code while an ISR for the same flag
was concurrently enabled. The negative HFIR-override and
`PCGCCTL`-is-clear findings from Step 34 remain valid observations
(those registers really are configured as shown), they just don't
mean what we thought they meant.

One residual anomaly not yet explained: even with the ISR disabled and
the direct `GINTSTS.SOF` flag confirmed to be reasserting normally
(~7-8/5ms), the `HFNUM` low-16-bit `FRNUM` field sampled in the same
diagnostic window (same IRQ-disabled interval) still read as static
(`changed=0` across the 20×1ms sweep) while the upper 16-bit `FTREM`
field visibly changed between samples. This inconsistency (SOF flag
demonstrably toggling, but the frame-number counter it's supposed to
be tied to appearing static in the *same* test run) has not been
explained yet and needs a fresh look — possibly worth an independent,
minimal repro (read `HFNUM` in a tight loop with no `k_msleep` at all,
purely time-boxed via `k_busy_wait`/DWT cycles, matching the successful
`GINTSTS.SOF` probe's polling style exactly) before drawing any further
conclusions from `FRNUM` specifically.

### Updated state of the investigation

- The original user-facing symptom (USB camera on "USER USB"/CN13
  never enumerates; `HCINT=0x80`/TXERR; zero D+/D- electrical
  activity) is still unresolved.
- Confirmed *not* the cause: static register misconfiguration (clock
  tree, GPIO/pinmux, PWR, HFIR, PCGCCTL), reset timing, VBUS
  capability, channel-0 collision, Ethernet/EMI or AHB-bus contention,
  CM4/dual-core contention, and (as of this step) SOF/frame-timer
  generation itself.
- Still open: why the D+/D- lines never show any electrical activity
  and `HCINT=0x80` (TXERR) results, given that the core's basic
  clocking, port-enable state, and now-confirmed SOF generation all
  look correct. The next productive angle is likely to look at what
  happens electrically/logically in the brief window between
  `USB_HC_StartXfer()` and the TXERR result specifically -- e.g.
  whether the channel's `HCCHAR.CHENA` bit is actually latching
  correctly relative to a real SOF boundary now that we know SOF truly
  exists, or whether some other one-shot state (FIFO empty flags,
  `HNPTXSTS`, per-channel `HCCHAR.ODDFRM` bit selection) differs
  between the two environments.
- All experiment code (Ethernet-gating test, CM4/BCM4 check,
  IRQ-disable-during-probe) remains in `stm32_hcd_adapter.c`'s
  `rawtest` path as additive diagnostics; no production-path logic was
  changed. Board's `BCM4` option byte was restored to its
  original (`1`, CM4 auto-boot enabled) state.

## Step 36 — HNPTXSTS diagnostics show hardware *can* succeed under Zephyr; jitter proves a timing-phase collision in `rawtest`; jitter/retry-budget fixes do NOT carry over to the production path

### `rawtest` route: proof the transfer can succeed, and that failure is phase-dependent, not random

Added `HNPTXSTS`/`HCCHAR`/`HCTSIZ`/`HFNUM` register dumps immediately
before/after `USB_HC_StartXfer()` in both `stm32_hcd_adapter.c`'s
`rawtest` routine and the bare-metal reference. Running `rawtest`
repeatedly (manually, via shell) produced a genuinely new result: on
one invocation it returned `HCINT=0x00000021` (`XFRC|CHH` — a fully
successful control transfer), immediately followed by a re-run that
returned `HCINT=0x00000080` (TXERR). This is the first confirmed
evidence that the raw hardware/HAL **can** complete a real SETUP
transaction correctly under Zephyr — the peripheral itself is not
fundamentally broken.

A 6-trial repeated-invocation test with a **fixed** ~2.5 s
inter-command delay showed a **reproducible-by-call-index** failure
pattern (e.g. run 2/run 3 always failed). Systematically ruled out as
the cause of that pattern, each confirmed by dedicated experiments:

- Channel-0 stale state / concurrent contention (pre-existing
  halt-before-test mitigation already handles this).
- TX FIFO/queue accumulation across repeated calls (added
  `USB_FlushTxFifo()` before every `StartXfer` — no change).
- LED blink-thread background activity (added
  `board_blink_threads_suspend()`/`resume()` in `board_io.c`, wired
  into `rawtest` — no change).
- Ethernet AHB1ENR clock gating during the test window (gated
  `ETH1MACEN/TXEN/RXEN` around the whole channel-init-through-poll
  window, not just the earlier SOF probe — no change).
- A blanket `irq_lock()` around `StartXfer` — inconclusive/likely
  harmful (produced a *worse*, different failure signature,
  `HCINT=0x00000000`, on the runs that still failed).

The single most useful result: re-running the same 6-trial test with a
**jittered** (0–2 s random) inter-command delay instead of a fixed one
caused the *specific failing run index to shift* (from run2/run3 to
run3/run4). Changing only the timing of when each attempt starts
changed which attempt failed — this is the signature of a genuine
**timing-phase collision** with something periodic, not random noise
and not monotonic FIFO/state degradation. The periodic culprit itself
was not identified (blink threads, Ethernet, and FIFO staleness were
all separately ruled out as *that* culprit too).

### Production-path experiment: retry jitter and a larger retry budget

Reasoned that production's `CTRL_ERROR` retry path
(`usbh_ctlreq.c`) and outer reconnect path (`usbh_core.c`
`HOST_DEV_ATTACHED`) retry from an **exactly repeatable relative
timing offset** each time (deterministic state-machine code path with
a fixed delay), so if that fixed phase happens to be bad, production
would never escape it on its own — while manual, human/shell-paced
`rawtest` invocations naturally sample different phases and often
succeed. Implemented:

- A jittered delay before re-issuing `SETUP` on `CTRL_ERROR`
  (`usbh_ctlreq.c`), using `k_cycle_get_32()` as an entropy source
  (not `SysTick->VAL`, which is not guaranteed to be running under
  Zephyr's own tick source and was confirmed to give no real jitter).
- A jittered delay in the outer `HOST_DEV_ATTACHED` reconnect path
  (`usbh_core.c`).
- `USBH_MAX_ERROR_COUNT` raised from `0x02` to `0x0A` (`usbh_def.h`)
  so jitter has more retry attempts to find a working phase before
  paying for a full reconnect/reset cycle.

Widened the jitter ranges twice (first attempt used only a few raw
CPU cycles/milliseconds of range — negligible — then widened to
100+0..199 ms outer / 5+0..49 ms inner). **Result: no change.**
6/6 manual `vbuscycle` reconnect attempts with the camera attached
still failed every single time, with byte-identical register
snapshots (`HCTSIZ=0x60080008`, `HCCHAR=0x20100040`/`0x00100040`)
every attempt. **The jitter/retry-budget fix that seemed so promising
from the `rawtest` experiments does *not* transfer to the production
enumeration path.** This is left in the tree as a harmless robustness
improvement (more retry budget with jitter can only help, never hurt
correctness), but it is **not the fix**.

### `HAINTMSK` investigation (false lead, ruled out)

Every production failure printed `HCINT=0x00000000` (as opposed to
`rawtest`'s `HCINT=0x80` TXERR) — a *different* signature. Added a new
`usbhost regs` shell command (`usb_host_shell.c`) to dump live
`GINTSTS`/`GINTMSK`/`HAINT`/`HAINTMSK`, hypothesizing the top-level
host-channel interrupt mask (`HAINTMSK`) might never be getting set in
production, which would mean `HAL_HCD_IRQHandler()` never runs for
channel 0 and only a software timeout could ever finalize the URB —
matching an all-zero `HCINT`.

Added a diagnostic print directly inside `USBH_LL_OpenPipe()`
(`stm32_hcd_adapter.c`) showing `HAINTMSK` immediately after
`HAL_HCD_HC_Init()` returns: it correctly shows `0x00000003` (both
control pipes' channel-interrupt-enable bits set) right after every
single pipe open, on every single reconnect. The earlier `0x00000000`
readings from the `usbhost regs` command were just unlucky snapshots
taken in the brief window between `USBH_FreePipe()` (on
`CTRL_ERROR` exhaustion) and the next reconnect's `OpenPipe` — not a
real bug. **`HAINTMSK`/interrupt wiring is correctly configured during
the actual transfer window; this is not the cause.** The
`HCINT=0x00000000` production signature is most likely just a normal
post-IRQ-clear artifact (the handler already serviced and
write-cleared the event bits by the time our diagnostic print runs),
not evidence of a literal "nothing ever happened" hang — this
reconciles the two signatures (rawtest's `0x80`, production's `0x00`)
as probably the *same* underlying XACTERR-family event, just observed
at different points relative to the IRQ's own register-clearing.

Also checked the VBUS load switch's `PG7_nFAULT` pin across every
attempt in this segment's logs: it never trips (`PG7_nFAULT=1`,
i.e. no fault, on every single sample). This rules out a hard
overcurrent/short condition on VBUS as an explanation, though it does
not rule out a softer voltage-droop that never crosses the load
switch's fault threshold.

### Updated state of the investigation

- The original symptom (camera on "USER USB"/CN13 never lights up /
  never enumerates) is **still unresolved** as of this step.
- Newly confirmed: the raw hardware/HAL, under Zephyr, genuinely *can*
  complete this exact control transfer successfully at least
  sometimes (`rawtest` proof). Production's continuous
  connect/retry/fail loop, however, shows **0% success even with a
  much wider, real jittered retry** — meaning either (a) the
  phase-collision theory that explained `rawtest`'s ~50% split does
  not apply the same way to the full production init/enumeration
  sequence (different code path, different timing profile before the
  first `SETUP`), or (b) there is a second, independent factor in the
  full production environment (background HTTP/network activity,
  cumulative device/board state across many rapid reconnects, or a
  power-domain effect) that a short-lived, isolated `rawtest`
  invocation doesn't reproduce.
- Ruled out this step: `HAINTMSK`/host-channel interrupt masking,
  VBUS hard overcurrent (load-switch fault pin).
- Still ruled out from before: static register misconfiguration,
  reset timing, channel-0 collision, Ethernet/AHB contention, FIFO
  staleness, LED blink threads, CM4/dual-core contention, SOF/frame
  timer generation itself.
- Not yet tried: suspending the HTTP server thread/TCP-IP stack
  entirely (not just its AHB clock) during a full production
  connect/enumerate attempt; instrumenting with a real oscilloscope or
  DMM on VBUS/D+/D- during a live camera-connect attempt (no such
  instrument is available to this investigation channel); trying a
  different/lower-power USB device on CN13 to see if the failure is
  device-current-draw-dependent.
- Uncommitted-at-write-time changes from this step: `HNPTXSTS`
  diagnostics + `USB_FlushTxFifo()` call in `rawtest`
  (`stm32_hcd_adapter.c`), `board_blink_threads_suspend/resume()`
  (`board_io.c`/`.h`), jittered retry delays in `usbh_ctlreq.c` and
  `usbh_core.c`, `USBH_MAX_ERROR_COUNT` raised in `usbh_def.h`, and the
  new `usbhost regs` shell command plus `USBH_LL_OpenPipe()` debug
  print in `usb_host_shell.c`/`stm32_hcd_adapter.c`.

## Step 37: comparing against ST's official STM32CubeH7 USB_Host examples — two more dead ends

Directly compared our `stm32_hcd_adapter.c`/`usb_host_shell.c` against
ST's own official reference USB_Host applications (STM32CubeH7 v1.13.0,
`Projects/STM32H747I-DISCO/Applications/USB_Host` and
`Projects/NUCLEO-H723ZG/Applications/USB_Host`, sparse-cloned to
`/tmp/cubeh7_usbhost_ref`, scratch/not committed).

Note: DISCO's example uses `USB_OTG_HS` with an external ULPI PHY
(`HCD_PHY_ULPI`, `HS` speed) — not comparable to our OTG_FS embedded
PHY. `NUCLEO-H723ZG`'s example, despite also naming its peripheral
`USB_OTG_HS`, actually configures `HCD_PHY_EMBEDDED` + `HCD_SPEED_FULL`
(the HS controller's built-in FS-only embedded PHY) — a much closer
functional analog to our board and the one used for comparison below.

### Hypothesis 1: `Sof_enable` — dead end (cosmetic only)

Both reference examples set `hhcd.Init.Sof_enable = DISABLE`; our
`stm32_hcd_adapter.c` had it `ENABLE`'d. Changed ours to `DISABLE` to
match and reflashed/retested with the physical camera — **identical
failure**, no change whatsoever.

Root cause of no effect: `stm32h7xx_ll_usb.c`'s `USB_HostInit()` (host
mode) unconditionally ORs in `GINTMSK_SOFM` regardless of
`cfg.Sof_enable` — that config field only gates SOFM in the
device-mode path (`USB_DevInit()`). Verified live via the `usbhost
regs` shell command: `GINTMSK` bit 3 (SOFM) stayed set to 1 both
before and after the config change. This means ST's own reference
examples *also* get a live 1 kHz SOF interrupt in host mode identically
to us — the config field is a no-op in host mode in this HAL version,
so this was never a real behavioral difference. Reverted to `ENABLE`
(matches our own `HAL_HCD_SOF_Callback()`/`USBH_LL_IncTimer()` usage
which relies on real SOF events); left the finding documented in a
comment in `stm32_hcd_adapter.c` so it isn't re-investigated.

### Hypothesis 2: tight busy-loop `USBH_Process()` polling — dead end (and briefly regressive)

ST's reference calls `MX_USB_HOST_Process()` -> `USBH_Process()` from
a zero-delay bare-metal `while (1)` superloop; our
`usb_host_poll_loop()` thread instead did `k_msleep(1)` between calls.
Hypothesis: `k_msleep(1)` rounds up to a full kernel tick and
introduces tick-aligned jitter, servicing the USBH state machine far
less often/more irregularly than the reference's tight loop, possibly
missing a NAK-retry or setup-phase timing window.

Tried replacing `k_msleep(USB_HOST_POLL_PERIOD_MS)` with `k_yield()` to
approximate the reference's tight loop. Rebuilt, reflashed: **serial
console went completely silent** (board otherwise alive: HTTP API and
LEDs kept responding/blinking normally). Root cause: `k_yield()` only
cedes the CPU to threads at the *same or higher* priority; it never
lets a lower-priority thread run. The USB poll thread runs at priority
`USB_HOST_POLL_THREAD_PRIORITY` (7) while the shell backend thread runs
at Zephyr's (lower, higher-numbered) default preemptive priority, so
the busy-looping poll thread completely starved the shell thread of
any CPU time. This is worse than a no-op: a real regression that would
have silently broken the console.

Reverted to `k_msleep(USB_HOST_POLL_PERIOD_MS)`, rebuilt, reflashed,
confirmed console fully responsive again (all standard shell/LED/HTTP
checks pass). Re-tested the **original symptom** (camera on CN13,
`usbhost vbuscycle`) on this known-good build: **failure signature is
byte-for-byte identical** to every prior test —
`HCINT=0x00000000`, `HC_state=2`, `HCTSIZ=0x60080008`,
`HCCHAR=0x20100040`/`0x00100040`, "Control error: Device not
responding", repeating every reconnect cycle.

### Net result

Both hypotheses from the ST-reference diff are ruled out. Comments
documenting *why* each is a dead end were left in
`stm32_hcd_adapter.c` (Sof_enable) and `usb_host_shell.c` (poll-loop
scheduling) so future debugging sessions don't waste time
re-investigating either. The underlying root cause of the 100%
control-transfer failure on CN13 remains unidentified.

- Ruled out this step: `Sof_enable` config field (host-mode SOFM is
  unconditionally forced on regardless of this setting — proven via
  live register read), and `USBH_Process()` poll-loop granularity via
  `k_yield()` (no behavioral difference found before console got
  starved; reverted to `k_msleep(1)`, which the reference's tight loop
  doesn't need since it isn't running under a preemptive scheduler
  with other threads to share CPU with).
- Still ruled out from before: everything listed in Step 36 and prior
  (LED threads, Ethernet AHB clocks, FIFO staleness, HAINTMSK masking,
  VBUS overcurrent, static register misconfig, SOF/frame-timer
  generation itself).
- Not yet tried: NVIC interrupt priority comparison (our
  `IRQ_CONNECT(STM32_HCD_IRQ, STM32_HCD_IRQ_PRI=5, ...)` vs. the
  reference's `HAL_NVIC_SetPriority(OTG_HS_IRQn, 6, 0)` — could still
  matter if a higher/equal-priority Zephyr kernel interrupt is
  preempting/delaying USB IRQ servicing at exactly the wrong moment);
  full source diff of `Middlewares/ST/STM32_USB_Host_Library` core
  state machine against our vendored `third_party/stm32-mw-usb-host`
  beyond the previously-bumped `USBH_MAX_ERROR_COUNT`; oscilloscope/DMM
  instrumentation (still unavailable).

## Step 38: user-requested experiment — run Zephyr's HCD servicing in the same synchronous-polling style as bare-metal

User directly asked whether Zephyr's USB host code could be changed to
service the OTG_FS controller the same way the working bare-metal
reference does: bare-metal never enables the OTG_FS NVIC interrupt at
all — it is a pure super-loop that (implicitly, via `HAL_HCD_...()`
polling helpers) reads/dispatches controller events synchronously,
with no ISR ever preempting anything.

### What was implemented

Added a runtime-toggleable "poll mode" to `stm32_hcd_adapter.c`/`.h`
and `usb_host_shell.c`:

- `stm32_hcd_adapter_set_poll_mode(bool)` / `_get_poll_mode()`: when
  enabled, permanently `irq_disable(STM32_HCD_IRQ)`s the OTG_FS NVIC
  line (this is a **per-peripheral** IRQ disable, not a global
  interrupt lock — UART/console and Ethernet have their own separate
  NVIC lines and are completely unaffected).
- `stm32_hcd_adapter_poll_service()`: calls `HAL_HCD_IRQHandler(&hcd_handle)`
  directly. This is the *same* handler function the ISR would have
  called — only *how* it gets invoked changes (synchronously, back to
  back with `USBH_Process()`, in the same thread) instead of
  asynchronously pre-empting other code.
- `usb_host_poll_loop()` (in `usb_host_shell.c`) now calls
  `stm32_hcd_adapter_poll_service()` immediately before
  `USBH_Process()` on every iteration, when poll mode is active.
- New shell command: `usbhost pollmode [on|off]`.

### Test result (with the physical camera on CN13)

`usbhost pollmode on` + `usbhost vbuscycle`: **enumeration still
fails**, same overall "unrecovered error" / "Control error: Device not
responding" outcome as async/interrupt-driven mode.

However, the **observed HCINT signature changed**: in normal
(interrupt-driven) mode, `HCINT` was seen stuck at `0x00000000` the
entire time (i.e. the channel appeared to never receive any hardware
event at all before hanging). In poll mode, `HCINT` now reads
`0x00000002` (bit 1 = `CHH`, Channel Halted) at the point the
middleware samples it. Tracing `stm32h7xx_hal_hcd.c`'s `CHH` handling:
a channel reaching `HC_HALTED` after `HC_XACTERR`/`HC_DATATGLERR` (i.e.
after a genuine hardware `TXERR`/transaction-error event) increments
`ErrCnt`; after `ErrCnt > 2` it sets `urb_state = URB_ERROR`, which is
exactly the "unrecovered error"/"Device not responding" path the
middleware reports. This means the *real* underlying event is a
`TXERR` (transaction error — the device did not respond to the
transaction inside the bus timeout window) happening repeatedly (3+
times), with our debug printf only sampling the residual `CHH` bit
after the driver has already cleared/processed `TXERR` internally.
This is consistent with, and does not contradict, every prior
finding in this investigation: the device genuinely is not answering
the host's transaction in time, under both async and now synchronous
polling. Switching to synchronous polling changed what got sampled/
printed but not the underlying hardware outcome.

### Conclusion

Poll mode does **not** fix the enumeration failure, but is being kept
as a permanent diagnostic tool (`usbhost pollmode on|off`) since it
lets future investigation directly compare interrupt-driven vs.
synchronous-polling behavior on demand without needing to reflash.
Confirmed the toggle itself is safe: disabling only the OTG_FS IRQ
line does not affect the shell console or Ethernet/HTTP, both of which
were re-verified fully functional with poll mode on and after turning
it back off.

- Ruled out this step: async-ISR-vs-synchronous-poll servicing style
  as the root cause — same ultimate TXERR/"device not responding"
  outcome either way, only the diagnostic sampling of `HCINT` differed
  cosmetically.
- New tool added: `usbhost pollmode [on|off]` — safe, reversible,
  disables only the OTG_FS NVIC line (not a global interrupt lock).
- Still ruled out from before: everything in Steps 1-37.
- Not yet tried: NVIC interrupt priority comparison; full
  `STM32_USB_Host_Library` core diff beyond `USBH_MAX_ERROR_COUNT`;
  oscilloscope/DMM instrumentation (still unavailable); trying a
  different/lower-power USB device on CN13.

## Step 39: Investigated Zephyr's new native USB Host (UHC) DWC2 driver

Followed up on whether upstream `zephyrproject-rtos/zephyr` code
(`drivers/usb/common/stm32/*.c`, `drivers/usb/uhc/uhc_dwc2.c`) linked
by the user could replace/fix our HAL-bridge approach.

### Findings

- Our pinned Zephyr (`v4.2.2`, commit `dbb536326e6`) already ships
  the generic USB host framework (`subsys/usb/host/usbh_core.c` +
  `include/zephyr/drivers/usb/uhc.h`) — this part is *not* missing.
- What's missing is only the DWC2 backend driver itself:
  `drivers/usb/uhc/uhc_dwc2.c` (2264 lines, 63KB) and its header,
  copyright-dated 2026, present only on upstream `main`.
- `drivers/usb/common/stm32/phy_*.c` are small PHY-power-quirk
  helpers used by the existing device-mode (`UDC_DWC2`) stack;
  `phy_embeddedfs_ulpi_off.c` only gates ULPI-sub-block sleep-mode
  clocking — irrelevant to our ULPI-less OTG_FS instance.
- Searched the **entire** upstream Zephyr tree (via GitHub API,
  recursive tree listing) for any board overlay, sample, or test
  referencing `uhc_dwc2` — **zero results**. No board anywhere has
  ever wired this driver to real hardware.
- Inspected `uhc_dwc2.h`: vendor-quirk hooks (clock/pinctrl/PHY/VBUS
  init) are hardcoded for exactly two SoCs —
  `espressif_esp32_usb_otg_fs` and `nordic_nrf_usbhs_nrf54l`. **No
  STM32 quirk implementation exists anywhere upstream.**
- Devicetree compatible also differs: our board's OTG_FS node uses
  `st,stm32-otgfs` (paired with the existing HAL-bridge/HCD driver);
  `uhc_dwc2.c` binds only to `compatible = "snps,dwc2"`, requiring a
  new overlay with properties (`num-in-eps`, `num-out-eps`,
  `ghwcfg1`, `phys`) that have no existing STM32H7 reference to copy.

### Conclusion

Using `uhc_dwc2.c` on our board is not a small backport — it would
require **authoring a brand-new Zephyr vendor-quirk driver from
scratch** for our exact SoC/peripheral (clock enable, pinctrl, PHY
power sequencing, VBUS GPIO), wiring a new devicetree overlay with no
prior art, on top of a driver with zero real-hardware validation
anywhere upstream. Assessed as high-effort/high-uncertainty (likely
multi-day, may not even build cleanly) relative to expected payoff,
since the underlying symptom (genuine hardware TXERR / device not
responding within bus timeout, Steps 1-38) has already been
characterized as timing/signal-level, not stack-architecture-level.
Not attempted further without explicit sign-off given the scope.

- Ruled out: "just recompile/enable a Kconfig option" framing of the
  user's suggestion — the real cost is writing new driver code, not
  flipping a switch.
- Still not tried: oscilloscope/DMM on D+/D-/VBUS; different USB
  device on CN13.

## Step 40: Clarified "hardware TXERR" wording + ruled out cache/MPU

User pushed back on Step 39 summary phrasing ("real root cause still
same: genuine hardware TXERR") — correctly pointed out this reads as
"hardware is broken," which contradicts Steps 1-38 where the
bare-metal super-loop harness enumerates successfully on the *same*
silicon/HAL. Correction: `TXERR`/"device not responding" is a
symptom reported by the DWC2 core (the device missed the host's
response-time window on the wire) — it is not proof the hardware
itself is faulty. Something Zephyr's environment does (still
unidentified) causes that miss; bare-metal's tight interrupt-free
super-loop does not trigger it.

Checked one more candidate differentiator this step: cache/MPU
configuration.

- Confirmed `soc/st/stm32/stm32h7x/soc_m7.c: soc_early_init_hook()`
  unconditionally calls `sys_cache_instr_enable()` +
  `sys_cache_data_enable()` at boot — Zephyr enables I/D-cache same
  as the bare-metal harness's `CPU_CACHE_Enable()`.
- Confirmed both bare-metal and Zephyr HCD init set
  `Init.dma_enable = DISABLE` (slave/FIFO mode) — no DMA-vs-cache
  coherency issue possible on either side, since the CPU pushes SETUP/
  data bytes straight into the OTG core's TxFIFO register, not
  through cached AHB SRAM.
- Bare-metal's MPU_Config is a generic whole-address-space background
  region (copied ST boilerplate), not a USB-buffer-specific
  carve-out — no meaningful MPU difference identified vs. Zephyr's
  default MPU map either.

### Conclusion

Cache/MPU config ruled out as the differentiator. The real
differentiator remains Zephyr's runtime environment (scheduler,
background threads, NVIC-driven concurrent activity) vs. bare-metal's
interrupt-free tight loop — already evidenced by the phase-shift
jitter behavior documented in earlier steps — not yet pinned to one
specific mechanism (exact IRQ/thread that delays the response past
the device's timeout window is still unknown).

- Ruled out this step: cache enable state, MPU region config, DMA
  mode as differentiators.
- Wording correction: replace "hardware TXERR" framing going forward
  with "hardware-reported transaction-timeout symptom, root cause
  still Zephyr-environment-side."

## Step 41: Clarified device-mode CDC-ACM success is not "software stack" vs hardware

User asked whether the working Step 26 Device-mode CDC-ACM test means
Zephyr was "using the USB stack in software" (implying Host mode
somehow uses a different, less-proven hardware path).

### Finding

Both directions use the identical DWC2 hardware core via the same
architectural pattern — an interrupt-driven ST HAL bridge, not a
bit-banged/software-emulated protocol:

- Device mode (Step 26 test): `CONFIG_USB_DEVICE_STACK=y` (legacy
  Zephyr USB device stack) uses `drivers/usb/device/usb_dc_stm32.c`,
  which wraps ST's `HAL_PCD_*` API (`HAL_PCD_Init`,
  `HAL_PCD_IRQHandler`), interrupt-driven, DMA disabled.
- Host mode (ours): `stm32_hcd_adapter.c` wraps ST's `HAL_HCD_*` API
  (`HAL_HCD_Init`, `HAL_HCD_IRQHandler`), same interrupt-driven
  pattern, same DMA-disabled slave/FIFO mode.

Both run under the identical Zephyr scheduler/thread/NVIC
environment. There is no "software vs. hardware" distinction between
them — same silicon, same HAL family (PCD/HCD are sibling APIs over
the same DWC2 core), same servicing style.

### Implication

The Step 26 success proves Zephyr's runtime environment can meet
real-time USB transaction timing on this exact hardware — it is not
a blanket "Zephyr's scheduler is too slow for USB" problem. This
narrows suspicion specifically onto the **host-role (HCD) code path**
— channel/FIFO configuration, SETUP-transaction sequencing, or
something host-role-specific in `stm32_hcd_adapter.c` or ST's HCD HAL
layer — rather than the Zephyr environment in general.

- New concrete next step: diff `stm32_hcd_adapter.c`'s FIFO/channel
  setup (`HCTSIZ`, `HCCHAR`, `GRXFSIZ`/`GNPTXFSIZ`/`HPTXFSIZ`
  allocation) against `usb_dc_stm32.c`'s equivalent device-mode FIFO
  setup, since the device-mode path is proven working on this silicon
  under Zephyr.

## Step 42: Reconfirmed Device-mode CDC-ACM success live, on demand

User connected CN13 to this PC and asked to re-check. Reflashed the
existing Step 26 CDC-ACM device-mode build
(`/tmp/stm32h7-zephyr-cdc-acm`, unmodified since 2026-09-16) live:

```
$ lsusb
Bus 003 Device 064: ID 2fe3:0001 NordicSemiconductor Zephyr CDC ACM sample
```

Bulk data path re-verified too:

```python
s = serial.Serial('/dev/ttyACM1', 115200, timeout=2)
s.write(b'Hello USB!\r\n')
# => b'Hello USB!\r\n'
```

Full enumeration + echo reproduced exactly as Step 26. Re-flashed
the normal Host-mode `apps/zephyr-hello-shell` build immediately
after to restore the board to its default debugging state.

- Reconfirms Step 26/41: Device-mode path on this exact board/
  connector/silicon is still 100% solid on demand, days later.
  Nothing has regressed on that side. The bug remains isolated to
  the Host-mode (HCD) path.

## Step 43: Restored board to Device-mode (fresh build) for demo prep

User decided to pursue a live-video demo and asked to restore the
board to USB Device mode as a starting baseline, ahead of building
either a USB MSC (stored-file) demo or an Ethernet-streaming demo.

Rebuilt (not just reflashed the stale artifact) the stock
`samples/subsys/usb/cdc_acm` sample fresh via:

```
west build -p always -b nucleo_h755zi_q/stm32h755xx/m7 \
  samples/subsys/usb/cdc_acm -d /tmp/stm32h7-zephyr-cdc-acm
```

(required `ZEPHYR_SDK_ROOT=/home/ranshal/.local/zephyr-sdk-1.0.1`,
`ZEPHYR_TOOLCHAIN_VARIANT=cross-compile` env vars, matching
`scripts/flash_zephyr_hello_shell.sh`'s toolchain detection.)

Flashed via `STM32_Programmer_CLI ... mode=UR reset=HWrst`. Verified:

```
$ lsusb
Bus 003 Device 065: ID 2fe3:0001 NordicSemiconductor Zephyr CDC ACM sample
```

Board is now in Device mode, ready as a base for the next demo step
(either extending this sample toward USB MSC file-transfer, or
building an Ethernet-based MJPEG/UDP live-video streamer per the
user's stated preference for Ethernet over USB for the "live" case).

## Step 44: USB Audio (UAC2) loopback demo — new bugs found, data path still stalls

Goal: play `~/Downloads/Valerie.mp3` from the host into the board and hear
it looped back, as a device-mode audio demo.

### Legacy stack (`samples/subsys/usb/audio/headset`) — real driver gap found

Built/flashed Zephyr's stock UAC1 `headset` sample. It enumerated fine as an
ALSA/PulseAudio sound card, but audio payload never arrived: UART logs
confirmed `HAL_PCD_DataOutStageCallback` never fired. Root cause: the legacy
`usb_dc_stm32.c` never implements `HAL_PCD_ISOOUTIncompleteCallback`, so
DWC2's normal `INCOMPISOOUT` condition permanently stalls the OUT endpoint
with no recovery path. This is a genuine gap in Zephyr's legacy STM32 USB
device driver — not board/hardware specific.

### Custom app on the modern stack (`apps/zephyr-uac2-loopback/`)

Since the newer `drivers/usb/udc/udc_dwc2.c` + `subsys/usb/device_next/class/usbd_uac2.c`
combo does handle `INCOMPISOOUT` recovery, wrote a standalone loopback app on
that stack (no stock sample fit — the two upstream UAC2 samples require
nRF5340 I2S hardware). Two real, confirmed bugs were found and fixed along
the way:

1. **`udc_stm32.c` hardcodes a 64-byte MPS cap on every non-EP0 OUT
   endpoint**, regardless of its actual USB type, rejecting any UAC2
   descriptor needing more (stereo/48kHz needs 192 bytes/ms). Worked around
   by using mono/24kHz (48 bytes/ms) instead of stereo/48kHz.
2. **`udc_stm32.c` never wired `CONFIG_UDC_ENABLE_SOF` to the ST HAL.**
   Every other in-tree UDC driver (dwc2, nrf, rpi_pico, kinetis, ambiq,
   numaker, it82xx2) does `IF_ENABLED(CONFIG_UDC_ENABLE_SOF, ...)` against
   its hardware SOF-interrupt-enable bit; `udc_stm32.c` `memset()`s
   `hpcd.Init` to zero and never sets `Init.Sof_enable`, so ST HAL's
   `USBx->GINTMSK |= USB_OTG_GINTMSK_SOFM` (gated on `cfg.Sof_enable != 0`)
   was **never** enabled at the hardware level no matter what the Kconfig
   said. Fixed directly in the vendored driver:
   `priv->pcd.Init.Sof_enable = IS_ENABLED(CONFIG_UDC_ENABLE_SOF);`
   in `udc_stm32_init()`. This is a real upstream-worthy fix.

Both fixes are real and correct, and after applying them the speaker
terminal's enable/disable state machine behaves correctly — it enables
once and **stays enabled** for the whole host-side playback session
(previously it may have toggled). This proves enumeration/negotiation is
fully healthy.

### Data still never arrives — confirmed, not a logging artifact

Despite both fixes, `data_recv_cb` in the app never fires — confirmed
three independent ways:
- Instrumented `recv_count`/`sent_count` counters, including an
  unconditional first-hit log line (not just every-200), never printed.
- `CONFIG_UDC_DRIVER_LOG_LEVEL_DBG=y` shows exactly **one** `udc_stm32_rx:
  RX ep 0x01` arm (at interface-alt-setting-enable time) and no
  `HAL_PCD_DataOutStageCallback`-driven `handle_msg_data_out: DataOut ep
  0x01` line ever, across a full ~15s playback window.
- Ruled out log-buffer drops as a false negative: switching to
  `CONFIG_LOG_MODE_IMMEDIATE=y` crashed the board (MPU fault / stack
  overflow, unrelated pre-existing fragility in that path) — reverted to
  deferred logging with `CONFIG_LOG_BUFFER_SIZE=8192`, which showed zero
  dropped messages during the actual playback window (drops only occurred
  during the noisy boot/enumeration burst, which doesn't matter here).
  `/tmp/valerie_recorded.raw` stayed 0 bytes across every run.

### Working hypothesis: shares root cause with the earlier USB Host failure

The user noted this "sounds similar to the usb host issue" from earlier in
this investigation (Steps 1-39): there too, enumeration/control transfers
succeeded but a class of periodic/high-throughput data transfer over this
same DWC2 silicon silently failed under Zephyr, while bare-metal HAL and
Zephyr's own bulk-based CDC-ACM device-mode transfers both work fine on
identical hardware. The pattern in both cases is "control-plane succeeds
completely, data-plane for isochronous/periodic transfers is silently
lost," strongly suggesting a shared, still-unidentified gap in how Zephyr's
STM32 DWC2 glue (both `udc_stm32.c` for device mode and the host-side HCD
adapter code) configures periodic/isochronous transfer state versus what
ST's own HAL does — not something fixable by further class-driver-level
patching. This remains the leading theory and is unresolved.

Current state: `apps/zephyr-uac2-loopback/` builds and enumerates
correctly with both real bugs fixed, but the loopback demo does not
audibly work. The two driver fixes (MPS-cap workaround via mono/24kHz
config, and the `Sof_enable` wiring in `udc_stm32.c`) are kept as genuine
improvements regardless of the demo's outcome.

## Step 45: USB Mass Storage demo — works cleanly, confirms bulk-only is the safe path

Given both isochronous device-mode (UAC2) and USB host mode remain broken
with the same suspected shared root cause (Step 44), built a USB Mass
Storage (MSC) demo instead: exposes a small in-RAM FAT-formatted disk as a
plain USB flash drive, using only bulk endpoints (control + bulk IN/OUT),
no isochronous transfers at all.

New app: `apps/zephyr-msc-demo/` — modern USBD stack
(`CONFIG_USB_DEVICE_STACK_NEXT` + `CONFIG_USBD_MSC_CLASS`), a 128KB
`zephyr,ram-disk` devicetree node, FatFS (`CONFIG_FAT_FILESYSTEM_ELM`,
`CONFIG_FILE_SYSTEM_MKFS=y` for auto-format-on-first-boot), and a
`README.TXT` written to the disk at boot for a visible "it's alive" marker.

**Result: works cleanly, end to end.**
- Enumerates as `2fe3:0009 ... STM32H7 Playground MSC Demo`.
- Host sees it as a real block device (`/dev/sdb`, 128K vfat).
- Mounted, read `README.TXT` back correctly.
- Wrote a new file from the host (`HOST.TXT`), confirmed content persisted
  and was byte-correct.

This confirms the earlier hypothesis: bulk-endpoint device-mode transfers
(already proven via CDC-ACM, Step 43, now again via MSC) are fully
reliable on this board under Zephyr. The failure mode is specific to
isochronous/periodic transfers (UAC2 device-mode OUT stream, and the
original USB host-mode enumeration path), not USB device mode as a whole.

**Practical takeaway for future demos on this board**: any USB device-mode
class built on bulk or control transfers (MSC, CDC-ACM, HID, DFU, custom
vendor bulk protocols) is safe to build on. Isochronous classes (UAC/UVC)
and USB host mode remain blocked pending the still-unidentified DWC2/UDC
periodic-transfer gap.

## Step 46: Combined demo — MSC + UART shell + Ethernet + HTTP server

Merged the working USB MSC demo (Step 45) with `apps/zephyr-hello-shell`'s
UART shell, LED control, Ethernet, and HTTP dashboard server, dropping only
that app's still-broken custom bare-metal USB host stack. Renamed the MSC
init code to `usb_msc.c`/`board_usb_msc_start()` and called it from a
combined `main.c` alongside `board_io_init()`, `board_blink_threads_start()`,
and `board_http_server_start()`.

Verified all four simultaneously on one flashed image:
- USB MSC: enumerates as `2fe3:0009`, RAM disk mounts as `/dev/sdb`.
- UART shell: `version` command over `/dev/ttyACM0` responds correctly.
- Ethernet: `ping 192.168.55.2` from host succeeds.
- HTTP server: `curl http://192.168.55.2/` returns the board I/O dashboard.

`apps/zephyr-msc-demo/` is now the full-featured board demo app going
forward; `apps/zephyr-hello-shell` remains as-is for USB host-mode
experiments only.

## Step 47: Low-speed mouse test isolates the remaining failure to the production USBH path

Rebuilt and flashed the host-enabled `apps/zephyr-hello-shell` image,
then tested a basic USB mouse in place of the camera. The mouse was
detected as a low-speed device:

```text
HPRT=0x00041805
```

`HPRT.PSPD=2` is low speed. VBUS remained present (`PA9=1`) and the
load-switch fault input remained inactive (`PG7_nFAULT=1`). The normal
ST USB Host middleware path completed connection and port reset, but
failed every first EP0 SETUP transaction exactly as it did with the
camera:

```text
ST USBH: USB Device Reset Completed
ST USBH: device connected
ST USBH: pipe 0 URB=4 HC_state=2 HCINT=0x00000000
HCTSIZ=0x60080008 HCCHAR=0x20100040 HPRT=0x00041805
ST USBH: unrecovered error
ST USBH ERROR: Control error: Device not responding
```

No valid device descriptor was received. A hard VBUS cycle did not
change the result.

### Low-speed EP0 MPS experiment: ruled out

Because the middleware initially opened the low-speed mouse's EP0 with
the library default MPS of 64, temporarily forced the initial low-speed
control-pipe MPS to 8. The live register confirmed the intended change:

```text
HCCHAR=0x20100008
```

The first SETUP still failed identically across repeated attempts.
The MPS change was therefore reverted. The original MPS-64 setting is
also not sufficient to explain the failure because the corrected raw
HAL test below succeeds with the same mouse and MPS 64.

### Rawtest contamination found and fixed

The first three mouse `usbhost rawtest` runs all returned `TXERR`.
Reviewing the test found that an obsolete diagnostic block forced:

```c
host_regs->HFIR = 60000U;
```

before the transfer. That mutation was harmless for the earlier
full-speed camera experiment, but it overwrote the HAL's correct
low-speed frame interval. Immediately before the overwrite, the mouse
run showed:

```text
HFIR=0x00001770
```

which is 6000 PHY-clock cycles for a 1 ms low-speed frame. Removed the
forced-HFIR experiment from `stm32_hcd_adapter_raw_control_test()` and
rebuilt/reflashed.

With HAL's low-speed timing left intact, all three repeated mouse
rawtests succeeded on the first poll:

```text
rawtest: USB_HC_Init -> 0 HCCHAR=0x00100040
rawtest: poll[00] HCINT=0x00000021 ... HCTSIZ=0x20000008
rawtest: HCINT went non-zero after 0 polls, stopping
```

`HCINT=0x21` is `XFRC | ACK`: the mouse accepted the SETUP transaction.
After those successful raw tests, resuming the normal middleware path
still produced repeated `TXERR`/`Device not responding` failures and
no descriptor.

### Updated conclusion

- The camera's current draw, UVC complexity, and camera-specific
  behavior are not the cause of the production failure. A low-power
  low-speed mouse fails at the same first EP0 SETUP stage.
- The connector, VBUS path, D+/D- path, OTG core, low-speed clock
  selection, and direct HAL transaction path all work with the mouse.
- Initial low-speed EP0 MPS 64 versus 8 does not explain the production
  failure.
- The old rawtest HFIR mutation was a real diagnostic bug and has been
  removed; future rawtests must not overwrite the speed-specific frame
  interval selected by the HAL.
- The decisive remaining comparison is now production USBH submission
  versus the successful raw HAL submission on the same device and
  boot. Capture the exact eight SETUP-buffer bytes/FIFO words and
  channel registers at `USBH_LL_SubmitURB()`/`USB_HC_StartXfer()`.
  A real USB protocol analyzer remains the strongest external check.

## Step 48: Production-versus-raw SETUP trace rules out packet, FIFO, and channel configuration

Added temporary tracing around production `USBH_LL_SubmitURB()` and
the raw `USB_HC_StartXfer()` call, then tested the same low-speed mouse
on one boot.

The production first request was correct:

```text
bytes=80 06 00 01 00 00 08 00
words=01000680,00080000
hc{addr=0 ep=0 in=0 speed=2 type=0 mps=64 pid=3 len=8}
after: HCCHAR=80100040 HCTSIZ=60080008 HNPTXSTS=010700fe
```

The same request means standard `GET_DESCRIPTOR(Device)`, request the
first eight bytes, address zero, endpoint zero, SETUP PID. The two FIFO
words are correctly little-endian packed. The raw test was changed to
use exactly those same bytes and the same channel (`0`) rather than its
old 64-byte request on channel `1`; it still completed first poll:

```text
rawtest: poll[00] HCINT=0x00000021 ... HCTSIZ=0x20000008
```

Repeated raw channel-0 runs succeeded 3/3 with the exact production
packet. A later run removed the rawtest's GPIO capture/global interrupt
lock and succeeded 5/5, so that short interrupt-masked capture window
is not responsible for the raw success.

The following production-only candidates were tested and ruled out:

- **OTG ISR execution:** fresh-boot `usbhost pollmode on` disables the
  OTG NVIC IRQ and directly services the same HAL handler in the poll
  thread. It still failed every SETUP with the residual `CHH` state.
- **Stale non-periodic Tx FIFO:** flushing all Tx FIFOs before every
  production SETUP changed `HNPTXSTS` but did not produce a descriptor
  or prevent `TXERR`.
- **Opening both control channels:** deferring the unused IN control
  pipe removed its early error report, but did not produce a completed
  OUT SETUP once the IN pipe was initialized after SETUP completion.
  This experiment was reverted.

All temporary trace, FIFO-flush, and middleware-order experiments were
reverted. The persistent rawtest corrections are intentional: it no
longer overwrites the HAL's speed-specific `HFIR`, and its descriptor
request/channel now exactly match production.

### Updated conclusion

The currently observable software state immediately before and after
`USB_HC_StartXfer()` is equivalent in the successful raw path and the
failing production path. The remaining difference is execution context
outside those snapshots: the full USBH state-machine scheduling and
concurrent system activity before the transfer. A USB protocol analyzer
is now the most direct way to see whether the physically transmitted
SETUP differs despite matching FIFO input. On-board software should next
measure the exact cycle timing from port-enable through channel arm, and
then isolate full production enumeration with the HTTP/network and
other application threads stopped.

## Step 49: Complete raw control transfer succeeds; production IRQ trace identifies SETUP TXERR

Extended `usbhost rawtest` beyond its previous SETUP-only check. It now
executes the complete first control transfer through the HAL:

1. SETUP OUT: `80 06 00 01 00 00 08 00`.
2. DATA IN: DATA1, eight bytes.
3. STATUS OUT: DATA1, zero bytes.

The OTG NVIC IRQ remains disabled during the transfer. A bounded polling
helper calls `HAL_HCD_IRQHandler()` synchronously so the non-DMA Rx FIFO
is drained and the HAL's normal channel state transitions still run.
All three repeated mouse tests completed every stage:

```text
rawtest: control stages setup_urb=1 data_submit=0 data_urb=1
data_count=8 status_submit=0 status_urb=1
rawtest: device descriptor[0:8] = 12 01 10 01 00 00 00 08
```

`URB=1` is `URB_DONE`, and the returned bytes are a valid USB device
descriptor prefix: length 18, device descriptor type 1, USB 1.10, EP0
MPS 8. This proves that SETUP OUT, DATA IN, Rx FIFO handling, and STATUS
OUT all work on the same low-speed mouse. The failure is not merely
hidden in the raw test's previously untested DATA or STATUS stage.

### Production interrupt-cause trace

The existing production error dump ran after the HAL had cleared the
meaningful channel interrupt, leaving only a halted/error state. Added a
temporary in-RAM trace around `HAL_HCD_IRQHandler()` and printed it only
after the middleware observed an error. Every production attempt showed
the same sequence:

```text
SUBMIT pipe=0 dir=0 token=0 len=8
IRQ pipe=0 HCINT=0x00000080 HCTSIZ=0x60080008 urb=0->0 hc=7
IRQ pipe=0 HCINT=0x00000002 HCTSIZ=0x60080008 urb=0->4 hc=2
```

`HCINT=0x80` is `TXERR`; `0x02` is the subsequent channel-halted
interrupt. `HCTSIZ` is unchanged, so production really fails during the
SETUP transaction and never enters DATA IN. The earlier residual `CHH`
logs were not timeouts and did not identify the original cause.

### Additional timing/load experiments ruled out

- Suspended both application LED blink threads before running production
  enumeration. SETUP still failed with the identical `TXERR` sequence.
- Added a true sub-frame phase sweep before production SETUP, cycling
  delays from 0 through 900 us in 100 us steps. Each offset was tested
  six or seven times; all failed with `TXERR`. The prior retry delay used
  whole milliseconds and therefore could not test this, but the direct
  sweep now rules out a narrow bad phase relative to SOF.
- Reviewed `HAL_HCD_HC_Init()`: it does not reset `ErrCnt`, channel state,
  or URB state, but those fields begin zero/idle on the first production
  attempt and the HAL clears `ErrCnt` after its internal error sequence.
  Stale software channel state does not explain the first failure.

The temporary IRQ trace, LED suspension, and phase-delay sweep were
removed after collecting the results. The complete raw control transfer
remains as a stronger diagnostic.

### Updated conclusion

The low-speed mouse completes the exact three-stage descriptor request
through the raw HAL path, while the production middleware's physically
first SETUP attempt receives an OTG-core `TXERR` before any bytes are
accounted as transferred. Request bytes, endpoint/channel configuration,
FIFO contents, MPS, interrupt-versus-poll servicing, LED load, and all
sub-frame launch phases tested so far are equivalent or ruled out.

The highest-value next step is an external USB protocol analyzer or
oscilloscope capture comparing raw and production packets on D+/D-. If
only on-board experiments are possible, reduce the raw path and
production path to two adjacent callable functions with an identical
pre-transfer core state, then remove rawtest preparation steps one at a
time. In particular, compare the effects of its long IRQ-disabled
diagnostic preamble and explicit channel halt/reset history; register
snapshots alone have not exposed their difference.

## Step 50: Full-speed SanDisk Cruzer reproduces the production-only failure

Replaced the low-speed mouse with a simple SanDisk Cruzer USB flash
drive. The host detected it as full speed:

```text
HPRT=0x00021405
```

Normal middleware enumeration repeatedly completed connection and port
reset, then failed the first control SETUP exactly like the low-speed
mouse and the camera:

```text
ST USBH: pipe 0 URB=4 HC_state=2
HCTSIZ=0x60080008 HCCHAR=0x20100040 HPRT=0x00021405
ST USBH ERROR: Control error: Device not responding
```

The complete raw control test then succeeded on the same Cruzer:

```text
rawtest: HCINT=0x00000021
rawtest: control stages setup_urb=1 data_submit=0 data_urb=1
data_count=8 status_submit=0 status_urb=1
rawtest: device descriptor[0:8] = 12 01 00 02 00 00 00 40
```

The returned prefix is a valid USB 2.0 device descriptor with EP0 MPS
64. Therefore the raw path now succeeds with both a low-speed mouse and
a full-speed mass-storage device, while production fails identically
with both.

This substantially rules out device class, low-speed-specific timing,
mouse/camera behavior, and insufficient device power as the root cause.
The Cruzer also keeps `PA9=1` and `PG7_nFAULT=1`; more importantly, it
completes the full raw transaction under the same supply and board load.
The remaining defect is specific to production-path preparation or
execution context before the first SETUP.

One diagnostic also reported `FLASH->OPTSR_CUR.BCM4=1`, meaning CM4
auto-boot was enabled. Step 51 repeated the CM4-disabled test with the
Cruzer and again found no change in the production failure.

## Step 51: Repeated CM4-disabled test with Cruzer; left CM4 boot disabled

At the user's request, repeated the earlier Step 35 CM4 isolation test
with the full-speed Cruzer. Readback first confirmed the starting state
was `BCM4=1`, then programmed the option byte and reset:

```text
BCM4 : 0x0 (CM4 boot disabled)
BCM7 : 0x1 (CM7 boot enabled)
```

No firmware rebuild was required; `BCM4` is a nonvolatile option byte,
not part of the CM7 Zephyr image. After allowing the CM7-only boot to
complete, the full application initialized normally, including GPIO,
Ethernet, HTTP, and USB host:

```text
Hello World! nucleo_h755zi_q/stm32h755xx/m7
usbhost: hardware ready
Camera stream listening on http://192.168.55.2:8081/
```

Production Cruzer enumeration still failed repeatedly at the same first
SETUP:

```text
ST USBH: pipe 0 URB=4 HC_state=2
HCTSIZ=0x60080008 HCCHAR=0x20100040 HPRT=0x00021405
ST USBH ERROR: Control error: Device not responding
```

The complete raw Cruzer transfer also succeeded while live diagnostics
reported `BCM4=0`, returning the same valid descriptor prefix:

```text
rawtest: control stages setup_urb=1 data_urb=1 data_count=8 status_urb=1
rawtest: device descriptor[0:8] = 12 01 00 02 00 00 00 40
```

This independently reconfirms that CM4 execution is not causing the USB
failure. Unlike Step 35, this session intentionally left `BCM4=0`
because the application does not use CM4. Final option-byte readback
confirmed CM4 boot disabled, and the USB retry loop was paused.

## Step 52: Root cause found — 1 ms USBH processing cadence is too slow

Reduced the successful raw path toward production one difference at a
time using the full-speed Cruzer. The complete raw control transfer
continued to succeed after removing each of these raw-only behaviors:

- the long IRQ-disabled diagnostic preamble;
- the explicit non-periodic Tx FIFO flush;
- the explicit channel halt;
- manual clearing of the HAL channel state;
- direct LL submission instead of `HAL_HCD_HC_Init()` and
  `HAL_HCD_HC_SubmitRequest()`.

The successful raw test also worked immediately after a production
failure without performing another port reset. None of those setup or
register-state differences caused the production failure.

The remaining architectural difference was call cadence. The Zephyr
host thread called `USBH_Process()` once per millisecond, while ST's
reference superloop and the raw transfer advance state continuously.
As a RED hardware test, changed only the enumeration state to call
`USBH_Process()` every approximately 10 us. This immediately completed
all descriptor control requests:

```text
ST USBH: PID: 5406h
ST USBH: VID: 781h
ST USBH: Address (#1) assigned.
ST USBH: Manufacturer : SanDisk
ST USBH: Product : U3 Cruzer Micro
ST USBH: Serial Number : 0877631638107AB2
ST USBH: Enumeration done.
```

The next `SET_CONFIGURATION` request initially failed because that
top-level state returned to the old 1 ms period. The production fix now
uses fast polling for all host states that initiate control requests
(`HOST_ENUMERATION`, `HOST_SET_CONFIGURATION`,
`HOST_SET_WAKEUP_FEATURE`, and `HOST_CLASS_REQUEST`) and while the
control state machine is otherwise active.

An unbounded fast loop starved Zephyr's lower-priority UART shell
thread. The final implementation therefore runs at most 99 iterations
spaced by 10 us, then sleeps for 1 ms to give other threads CPU time.
Paused mode always sleeps.

Fresh hardware validation with `BCM4=0` repeatedly completed:

```text
ST USBH: Enumeration done.
ST USBH: Default configuration set.
ST USBH: enumeration complete; no class driver registered
```

No `Device not responding` or control-transfer error occurred during
the validated run. The repeated disconnect/re-enumerate cycle afterward
is expected from this diagnostic application's explicit
`HOST_ABORT_STATE` transition when no USB class driver is registered;
it is separate from the fixed control-transfer failure.

### Updated conclusion

The USB electrical path, OTG core, HAL, and interrupt servicing were
working. The defect was the integration's fixed 1 ms
`USBH_Process()` scheduling interval. ST's middleware requires its
control state machine to be advanced more promptly between stages on
this target. Bounded fast processing fixes enumeration and
configuration while retaining scheduler fairness.

## Step 53: Camera still fails below middleware; raw SETUP succeeds only 1/5

Reconnected the HP camera after validating the bounded fast-processing
fix with the Cruzer. Unlike the Cruzer, the camera did not complete
production enumeration. The failure included both the control OUT pipe
and DATA IN pipe:

```text
pipe 0 URB=4 HCTSIZ=0x60080008 HCCHAR=0x20100040
pipe 1 URB=4 HCTSIZ=0x40080040 HCCHAR=0x20108040
HPRT=0x00021405 PA9=1 PG7_nFAULT=1
```

The complete raw test sometimes transferred SETUP and the first eight
descriptor bytes:

```text
12 01 00 02 ef 02 01 40
```

This identifies a USB 2.0 composite/IAD device with EP0 MPS 64. Later
production progress also identified it as HP `VID=03f0`, `PID=664a`.
However, that raw run remained `URB_IDLE` at STATUS OUT.

After a genuine 2-second VBUS power cycle, five paused raw trials gave:

- four SETUP failures, normally immediate `HCINT=0x80` (`TXERR`);
- one successful SETUP plus eight-byte DATA IN, followed by STATUS OUT
  remaining idle;
- zero complete three-stage control transfers.

The Cruzer repeatedly completes enumeration and `SET_CONFIGURATION`
using the same final firmware, cable/connector, host controller, and
bounded USBH processing. The camera therefore has an additional issue
below the middleware cadence fix. Its intermittent raw SETUP failure
points toward camera power integrity, signal integrity, or device-side
state rather than ST USBH state-machine scheduling.

`PA9=1` and `PG7_nFAULT=1` rule out sustained VBUS loss/overcurrent but
cannot detect a short VBUS droop. The board's stated 5 V / 0.6 A input
is also much more plausible as a limitation for a webcam than for the
Cruzer or mouse. The next decisive A/B test is the same camera through
a self-powered USB 2.0 hub, or with a stronger regulated 5 V supply,
while keeping the current firmware unchanged.

## Step 54: External 5 V supply does not improve camera reliability

Retested the reconnected HP camera after powering the board from the
external 5 V supply, without changing or rebuilding firmware. ST-LINK
was initially disconnected from the host; after reconnecting its USB
cable, both the UART and SWD probe were detected normally.

On a fresh hardware reset, production detected and reset the full-speed
camera and opened both EP0 pipes, but did not complete the first
descriptor request within the 20-second observation window:

```text
ST USBH: USB Device Reset Completed
ST USBH: device connected
USBH_LL_OpenPipe: pipe=1 epnum=0x80 ep_type=0 ret=0
USBH_LL_OpenPipe: pipe=0 epnum=0x00 ep_type=0 ret=0
```

Five paused raw control trials then produced almost the same distribution
as Step 53:

- one trial completed SETUP and eight-byte DATA IN, then remained
  `URB_IDLE` at STATUS OUT;
- four trials failed SETUP immediately with `HCINT=0x80` (`TXERR`);
- zero trials completed all three control stages.

Representative results:

```text
setup_urb=1 data_urb=1 data_count=8 status_urb=2
setup_urb=4 data_urb=0 data_count=0 status_urb=0
```

The stronger/external board supply therefore did not measurably improve
camera behavior. Ordinary board-input power limitation is no longer the
leading explanation. A self-powered USB hub would still test whether
isolating the camera's VBUS locally changes anything, but current
evidence points more strongly toward camera/cable signal integrity,
device-specific transaction behavior, or an OTG electrical/timing
interaction that the Cruzer does not trigger.

## Step 55: Post-cadence-fix low-speed mouse retest

With the mouse connected directly to CN13, rebuilt and flashed the
host-enabled hello-shell app with the bounded fast `USBH_Process()` cadence
from Step 52. The production host now reads the full device descriptor and
assigns address 1 (`VID=413c`, `PID=301a`, low speed), unlike the
pre-fix failure at the very first SETUP. In repeated runs it progressed
as far as the 34-byte configuration descriptor or the manufacturer
string (`PixArt`), then remained indefinitely in `CTRL_DATA_IN_WAIT`.
One snapshot of a 34-byte request showed 16 bytes received on the IN
channel, `URB_IDLE`, and 18 bytes still expected. Another run stalled
after eight bytes of a 255-byte product-string request. At one stall the
port had changed from enabled (`HPRT=0x00041805`) to disabled
(`HPRT=0x00041801`), despite `PA9=1` and `PG7_nFAULT=1`.

Isolated raw HAL control requests on the same mouse succeeded with an
18-byte device descriptor (three 8-byte packets) and with a 255-byte
product-string request that returned a valid 58-byte descriptor, including
the zero-length STATUS stage in each case. Thus the mouse supports
multi-packet control reads; the production failure is not explained
simply by a maximum packet size of eight bytes or by requesting more
string bytes than the device returns. Synchronous IRQ polling was also
tried on a fresh boot; it still failed after address assignment, with
repeated `URB_NOTREADY` on the control OUT channel.

**Current boundary:** the mouse is detected and partially enumerated, but
full configuration and HID input have not been validated. Investigate
why the production low-speed port becomes disabled or a control URB
remains idle; only then register a HID host class and poll interrupt-IN
mouse reports. The diagnostic-only code changes used in this step were
reverted.

## Step 56: Powered hub test reaches hub, not downstream mouse

After inserting a self-powered USB hub between CN13 and the mouse,
reset the board and resumed the unchanged host firmware. The root
port reported full speed (`HPRT=0x00021401`), and the host read the
hub's device descriptor: `VID=8564`, `PID=4100`, device class `0x09`
(hub), USB version 2.10. It assigned address 1, then remained in
`ENUM_GET_MFC_STRING_DESC` / `CTRL_DATA_IN_WAIT`; the port-enable
bit was clear at the final snapshot. No mouse descriptor was read.

The current host application has no hub class driver or downstream
port-management support. Detecting a hub on the root port therefore
cannot demonstrate whether the mouse behind it works. A direct mouse
connection remains the shorter path to testing HID; supporting this
hub first requires enumerating/configuring it, powering and resetting
its downstream port, and handling low-speed transactions through its
transaction translator, in addition to adding mouse HID support.

## Step 57: Direct mouse retest separates port disable from control failure

Reconnected the mouse directly to CN13 and confirmed the root port
again reported low speed (`HPRT=0x00041801`). With temporary event
diagnostics, the host read `413c:301a`, assigned address 1, and
sometimes read the manufacturer string (`PixArt`). One run stalled
indefinitely at `ENUM_GET_PRODUCT` / `CTRL_STATUS_OUT_WAIT`, repeatedly
reporting `URB_STALL` on the control OUT pipe while the port *remained
enabled* (`HPRT=0x00041805`, `port_enabled=1`). In another run, the
first `URB_NOTREADY` occurred on a SETUP transfer at an enabled port;
`unrecovered error` followed, and only then did the port-disable
callbacks occur while the host reset and retried enumeration.

Thus a disabled port in an earlier final snapshot is not established
as the initiating cause; the middleware/HCD control-transfer error
comes first in at least one captured run. The middleware's
`CTRL_STATUS_OUT_WAIT` handles `URB_DONE`, `URB_NOTREADY`, and
`URB_ERROR`, but does not handle `URB_STALL`, explaining why that
particular failure remains stuck without recovery. Why the low-speed
mouse produces the control-transfer STALL/NOTREADY under the production
host path is not yet resolved. Event diagnostics were removed after
this run.

## Step 58: Cadence A/B and boot-mouse interrupt transfers

On the directly attached Dell MS116 (`413c:301a`), the production host
completed enumeration and set its configuration on 2/6 fresh resets
with 10 us-spaced fast control polling, versus 16/16 fresh resets with
50 us-spaced polling. The latter is an empirical improvement on this
mouse, not an established explanation of the earlier failures or proof
of reliability across other devices.

Added a narrow ST-middleware HID boot-mouse class for the first
interface: it requires a boot mouse and a valid interrupt-IN endpoint,
sends HID `SET_PROTOCOL(BOOT)`, and decodes 3- or 4-byte movement,
button, and wheel reports. On fresh resets the host reached
`Boot mouse class started` and `boot mouse ready on EP 0x81
(interval 10 ms)`. The host did **not** receive a report, even during
continuous physical movement and clicks. Channel 2 returned
`URB_ERROR`, with its host-channel interrupt trace counting `TXERR`
(bit 7) and channel halted (bit 1) while the root port stayed enabled
(`HPRT=0x00041805`). Bounded 10 ms interval retries still yielded zero
reports. Switching to synchronous HCD IRQ polling for three resets did
not change that result (TXERR on all three). Delaying the first poll
100 ms after `SET_PROTOCOL` also did not help and was removed.

The working boundary is now **configured boot mouse, but no HID input**.
The interrupt-IN transaction errors remain unexplained; do not
interpret enumeration success as functional mouse support. The
boot-mouse class, 50 us cadence, and read-only channel-2 IRQ diagnostics
remain in the worktree for further investigation, uncommitted.

## Step 59: Raw HAL interrupt-IN reproduces TXERR below HID class

Added `usbhost rawin` as an isolated probe of the already configured
direct mouse: resume the host, wait for `boot mouse ready`, then run
`usbhost pause` and `usbhost rawin`. It requires the mouse class and an
enabled root port, halts the class's active channel, and submits
interrupt-IN requests directly through STM32 HAL on a separate host
channel, using the enumerated address, endpoint, packet size, speed,
and interval. It does not reset the port or change the device address.
It leaves the middleware paused; reset before resuming normal operation.

On three fresh resets the configured mouse was at address 1, low speed,
endpoint `0x81`, max packet size 4, interval 10 ms, with the port enabled
(`HPRT=0x00041805`). All 12 raw transfers on **each** reset saw
`HCINT=0x82` (`TXERR | CHH`); none transferred any bytes. HAL classified
every third attempt as `URB_ERROR` after its transaction-error counter
exceeded two, while the intervening attempts were `URB_NOTREADY`.
There were no successful DATA packets or NAK events in this probe.
This matches the production interrupt-IN channel's TXERR/CHH trace
and rules out the HID report decoder and the ST host class state
machine as the direct source of *these* interrupt-IN errors. It does
not establish whether the OTG host controller's low-speed interrupt
scheduling, PHY/bus signaling, device response, or another HAL-level
condition is the cause. No mouse-input success is claimed.

## Step 60: Host periodic scheduler A/B still returns TXERR

Using `usbhost rawin bulk` after the same configure/pause sequence
programs the **same mouse endpoint and DATA0 IN token** on probe
channel 3, but requests HAL's non-periodic bulk scheduling instead of
interrupt scheduling. This is a diagnostic controller setting, **not**
a valid replacement HID transport: the mouse's descriptor still
specifies interrupt-IN. Two fresh resets in each mode yielded 12/12
`TXERR | CHH`, zero bytes, no `NAK`, and an enabled low-speed port
per run. Both channels used address 1, endpoint `0x81`, MPS 4 and
10 ms spacing. The periodic TX FIFO had free space in sampled
`HPTXSTS` (`0x2d0800e0`). This A/B makes an issue exclusive to the
periodic request queue unlikely; it does not rule out a shared
HAL/controller timing or electrical problem.

The HAL speed value 2 matches both `USBH_SPEED_LOW` and
`HCD_DEVICE_SPEED_LOW`, and the controller's HCFG selects the 6 MHz
low-speed clock with HFIR=6000. Successful EP0 control transfers
already reach address 1 and configure the device, so the remaining
failure is specific to the nonzero mouse data endpoint or the stage
after configuration—not a blanket inability to address this
low-speed mouse. The experiment remains diagnostic and uncommitted.

## Step 61: Standalone STM32Cube HID host reads mouse movement and buttons

Built `experiments/cube-hid-mouse` for the exact NUCLEO-H755ZI-Q CM7:
official ST USB Host Core/HID middleware, STM32CubeH7 HAL, USB_OTG_FS
embedded PHY on PA11/PA12, HSI48 kernel clock, PD10 active-low VBUS,
and ST-LINK USART3 at 115200. The board's current `BCM4=0` option byte
was retained; no CM4 image or option-byte changes were needed. This
bare-metal host calls `USBH_Process()` continuously, in contrast to
the Zephyr app's scheduled polling. It uses a different HAL snapshot,
so it is a functional comparison, not a one-variable experiment.

Flashed its CM7 ELF with STM32_Programmer_CLI and tested the **same
directly attached** Dell/PixArt `413c:301a` mouse on CN13. UART logged
address assignment, descriptor strings, configuration, boot-mouse
interface recognition, and `USB HID active`. All three fresh resets
again configured and activated the HID class. During an explicit
12-second movement/button test, UART captured **1,147 mouse reports**
with signed X/Y deltas and button transitions (including `110` and
`011`). This proves a working mouse host on this board without a hub
or USB analyzer. Logs: `/tmp/cube-hid-first-boot.log`,
`/tmp/cube-hid-reset-{1..3}.log`, and
`/tmp/cube-hid-movement-live.log`.

The remaining failure is specific to the Zephyr-host combination,
not an inherent limitation of the board, VBUS path, mouse, or ST
middleware as a whole. The two builds differ in HAL version, USBH
class/driver implementation, scheduling/IRQ context, and application
load. Compare the first EP `0x81` host-channel events and frame timing
under both builds before assigning root cause to any one difference.
**The Cube image is currently flashed on the board**; Zephyr must be
reflashed to resume its shell/HTTP app.

The initial HAL-version A/B gave unreadable UART output at 115200:
the west workspace's HAL header defaulted `HSE_VALUE` to 25 MHz
instead of this board's 8 MHz. Consequently HAL calculated a fictitious
1.25 GHz clock from the real 400 MHz PLL and programmed USART3 for
approximately 36.8 kbaud. Reading USART3 BRR through SWD showed
`0x0a99` with the swapped HAL, versus `0x0364` in the known-good
build. At 38400 baud, that first swapped image already logged
configuration and `USB HID active`; the unreadable output was **not
a USB failure**.

For a controlled comparison, the bare-metal app was rebuilt against
`vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers`
with `-DHSE_VALUE=8000000U` for all translation units. The fixed image
logged the correct 400 MHz clock and USB HID activation at 115200 on
three fresh resets. During a 12-second move/click test it delivered
**1,033 movement/button reports**, including pressed buttons; further
resets produced reports as well. This vendor-HAL Cube image is the
current board firmware. Thus the **same HAL snapshot used by
Zephyr works on bare metal with the same mouse and STM32Cube HID
middleware**. The HAL version alone does not explain Zephyr's
interrupt-IN TXERR. Remaining differences include Zephyr's
USBH polling/IRQ scheduling, system configuration/load, and its
distinct boot-mouse class. Build command (output in `/tmp`):

```sh
make -C experiments/cube-hid-mouse \
  HAL_SRC=../../vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/src \
  HAL_INC=../../vendor/zephyrproject/modules/hal/stm32/stm32cube/stm32h7xx/drivers/include \
  BUILD=/tmp/cube-vendor-hal all
```

## Step 62: HID startup requests do not explain interrupt-IN TXERR

Compared Cube's HID startup to Zephyr's boot-mouse class on the same
directly connected mouse. In separate, single-change Zephyr trials:

- `SET_PROTOCOL(1)` (Cube's report-protocol value) configured on 4/4
  resets but delivered zero reports; channel 2 still recorded `TXERR`.
- An initial `GET_REPORT` returned `USBH_NOT_SUPPORTED` on 4/4 resets,
  as Cube's HID class explicitly tolerates. Subsequent interrupt-IN
  transfers still returned `TXERR` and zero reports.
- Synchronizing the first interrupt-IN poll to Cube's odd-frame
  transition still yielded `TXERR` and zero reports on 4/4 resets.
- `SET_IDLE(0)` succeeded on three configured resets, but subsequent
  interrupt-IN transfers still yielded `TXERR` and zero reports.
  A fourth reset stalled earlier in control enumeration and does not
  count as a class-level result.

Each Zephyr change was reverted before the next trial. For a stronger
class-sequence comparison, temporarily reduced the **working Cube
application using Zephyr's HAL snapshot** to Zephyr's shorter startup:
skip report-descriptor and `SET_IDLE` requests, send only
`SET_PROTOCOL(BOOT)`, skip initial `GET_REPORT`, and start polling
without Cube's initial frame synchronization. On three fresh resets
the Cube class still activated, and the first interrupt-channel event
was `NAK` (`HCINT=0x10`), not `TXERR`. Restored Cube's original class
code after this experiment. A `NAK` while the mouse is stationary is
normal; these particular eight-second captures did not include mouse
movement or button-report validation.

Read-only snapshots immediately before the first channel-2 IRQ show
the same endpoint, packet size, transfer size (`HCTSIZ=0x00080004`),
and interrupt mask (`HCINTMSK=0x0000079f`) in both systems. Cube
reported first IRQ `0x10` on three resets; Zephyr reported first IRQ
`0x80` on three resets with an enabled port and zero reports. The
channel-characteristic register differed in transient enable/disable
bits when sampled at the IRQ. The next investigation should compare
controller setup, clock/GPIO configuration, and execution/IRQ timing
between the two applications, rather than assuming Cube's HID startup
sequence is required. Root cause is still unknown; the restored
Zephyr boot-mouse class still cannot read mouse reports.

## Step 63: Controller state, pin configuration, and timing comparison

Captured matching registers at the **first channel-2 interrupt** in
Cube (with Zephyr's HAL snapshot) and Zephyr. Cube's first event was
`NAK` (`0x10`) and Zephyr's was `TXERR` (`0x80`). Both used channel 2,
endpoint `0x81`, `HCCHAR` endpoint/address/speed/type settings,
`HCTSIZ=0x00080004`, `HCINTMSK=0x0000079f`, HSI48 ready
(`RCC_CRRCR=0x170`), USB clock selection (`RCC_D2CCIP2R=0x00300000`),
`GUSBCFG=0x20001440`, `GCCFG=0x00010000`, `HCFG=0x202`, and
`HFIR=0x1770`. PA11/PA12 mode, AF10, pull, output type, and speed
also matched. The first IRQ frame counter and the transient
channel-enable/odd-frame bits varied, but neither identified a
consistent incorrect channel setting.

Cube configured PA10 (USB ID) as AF10 push-pull with no pull, while
Zephyr used AF10 open-drain with pull-up. Temporarily matching Cube's
PA10 settings in Zephyr still produced `TXERR` and zero reports on
three configured fresh resets; one additional reset stalled earlier
in control enumeration. Restored the original PA10 configuration.

The normal Zephyr build uses a 10 kHz OS tick and a 480 MHz CM7,
versus Cube's 1 kHz tick and 400 MHz CM7. In isolated experiments,
Zephyr at 1 kHz still yielded first-event `TXERR` on three
configured resets (one stalled before class initialization); Zephyr
at 400 MHz also yielded first-event `TXERR` on 4/4 configured
resets. Temporarily slowing Cube's HID-processing loop to one
iteration per millisecond **after** class activation still produced
first-event `NAK` on 3/3 resets. Restored the normal clock/tick
settings and continuous Cube loop afterward.

Finally, bypassed Cube's `HAL_HCD_Init()` and called the same
`USB_CoreInit()`, `USB_SetCurrentMode()`, and `USB_HostInit()` sequence
used by Zephyr, with Cube's existing GPIO/IRQ setup. Cube still
activated HID and received first-event `NAK` on 3/3 resets. Restored
its original HAL initialization after the experiment.

These controlled trials do **not** establish a working Zephyr mouse:
the captures were taken with a stationary mouse and did not check
new movement reports. The first-event contrast persists across
clock, tick, basic GPIO/USB register, and HCD-init comparisons.
The remaining differences are Zephyr's execution environment,
interrupt handling/lifecycle, other enabled peripherals, and
possibly hardware state not represented in the captured registers.
None of these is yet confirmed as the cause. Logs for this step are
under `/tmp/{cube,zephyr}-{first-system,pa10,tick1k,slow,400,manual}*`.

## Step 64: First interrupt-IN submission and hardware-frame A/B

No external USB protocol analyzer or oscilloscope is available. The
existing `usbhost arm` / `usb_gpio_capture.c` software capture was
already tried in Step 22: PA11/PA12 GPIO input readbacks were constant
zero while AF10 routed these pins to the USB PHY. Those samples cannot
distinguish a transmitted IN token from a NAK or absent mouse response.

Taking Ethernet interface 1 down **before** `usbhost resume` on four
fresh Zephyr resets did not change the result: all configured the
mouse and the first channel-2 IRQ remained `TXERR` (`0x80`), with zero
reports. This excludes active Ethernet-interface traffic as a
necessary condition, not all effects of the initialized networking
stack or other application threads.

Temporary register probes at the first interrupt-IN submission and
first channel IRQ recorded three resets in each image. Both used
channel 2, low-speed EP `0x81`, no split transaction
(`HCSPLT=0`), empty previous interrupt status, and the same periodic
TX FIFO status (`HPTXSTS=0x000800e0`), with the root port enabled
(`HPRT=0x00041805`) at the first event. Cube submitted on an even
hardware frame, set `HCCHAR.ODDFRM`, and received a NAK (`0x10`)
on the next frame. Zephyr submitted on an odd hardware frame, cleared
`ODDFRM`, and received TXERR (`0x80`) on the next frame.

To isolate **hardware** frame parity (not the middleware's SOF timer),
temporarily waited for even `HFNUM` before Zephyr's first mouse
channel submission. All five fresh resets configured, submitted on
an even frame with `HCCHAR.ODDFRM` set as in Cube, and still returned
first-event TXERR with zero reports. The frame-alignment trial and
all temporary submission probes were removed afterward.
Controller status alone does not establish whether the IN token was
put on the physical D+/D− pair, whether the mouse replied, or whether
the reply failed decoding. That boundary requires external bus
observation; do not treat the old software GPIO sample as proof.

## Step 65: Mouse-only Zephyr image still returns TXERR

Built a separate `experiments/minimal-zephyr-mouse/` application for the
same board with the **unchanged** production USB host adapter, class,
middleware, polling cadence, early UART/VBUS setup, and UART shell.
`CONFIG_NETWORKING=n` and both Ethernet MAC and MDIO nodes are disabled;
no HTTP, camera stream, or LED blink threads are started. `board_io.c`
remains linked for the existing `usbhost rawtest` command but its
blink-thread startup is never called. The minimal variant uses the same
480 MHz board clock and GPIO VBUS control as the normal image.

On four fresh resets with the same directly attached Dell/PixArt
`413c:301a` mouse, `usbhost resume` enumerated and configured the
mouse, completed `SET_PROTOCOL(BOOT)`, and logged `boot mouse ready`.
Each first channel-2 interrupt was `TXERR` (`HCINT=0x80`), with the
port enabled (`port_enabled=1`); after the observation window,
`usbhost status` showed `mouse: reports=0` every time. This rules out
active Ethernet, HTTP, camera streaming, or LED-blink threads as
**necessary** causes of the interrupt-IN failure. It does not rule
out a shared Zephyr/HAL integration difference, IRQ context,
controller behavior, or electrical timing. Logs:
`/tmp/minimal-mouse-reset-{1..4}.log`. The normal Zephyr shell image
was reflashed after the trial.

## Step 66: Cube host crossover isolates host-loop cadence; mouse input works

Ran Cube USB Host Core/HID middleware in a minimal Zephyr application
(`experiments/zephyr-cube-crossover/`). With Cube's platform bridge,
the first mouse-channel IRQ was `NAK` (`0x10`) on 4/4 fresh resets and
a user-assisted 20-second capture decoded 1,415 movement reports.
With the **original Zephyr HCD adapter** in place of Cube's bridge,
Cube HID again got first `NAK` on 3/3 resets and decoded 1,296
movement/button reports over 20 seconds. Thus the existing Zephyr
bridge can carry actual input, not just enumerate the device.

Keeping the Zephyr bridge and Cube Core but replacing Cube HID with
the application's boot-mouse class, at a continuous 50 us host loop,
gave first `NAK` on 3/3 resets. A further 20-second live capture
decoded 1,030 movement/button lines with no transfer errors. The
class and its decoder can also work without modification.

Changing only that class loop to a 1 ms sleep reproduced first-event
`TXERR` (`0x80`) and zero reports on 3/3 resets. Applying the same
1 ms sleep to Cube HID caused a post-activation control error, so it
does not independently identify the mouse transfer. A bounded loop
of 99 fast 50 us iterations followed by a 1 ms sleep yielded first
`NAK` on 3/4 resets but later TXERR retries and mouse-class abort;
the fourth was still enumerating during the observation window.
Continuous servicing, not merely a fast initial transfer, matters
in these trials. This is evidence for a timing-sensitive interaction,
not an electrical explanation of the OTG core's TXERR.

Updated the shared production USB host poll thread to run 50 us
iterations throughout `HOST_CLASS` and lowered its priority below
the shell (priority 9 shell, 10 USB host). Control transfers retain
their bounded fast bursts. The mouse-only Zephyr image then got first
`NAK` on 4/4 fresh resets; live movement/click testing decoded 980
input lines (1,358 reports total, 166 lines with buttons) without
transfer errors. The normal Zephyr image was built and flashed, got
first `NAK`, and decoded 919 movement/button lines (1,028 reports
total, 178 lines with buttons), also with no transfer errors.
UART shell, HTTP, LED control, and host-to-board ping continued to
work. Board-to-host ping at the historical `.100` address failed
because the host now has `192.168.55.101`; manually pinging `.101`
from the board succeeded. The 50 us busy loop consumes CPU while
USB is active, so a future event-driven scheduling solution would
need separate hardware validation before replacing it. Logs:
`/tmp/crossover-fast-boot-{1..3}.log`,
`/tmp/crossover-slow-boot-{1..3}.log`,
`/tmp/crossover-burst-boot-{1..4}.log`,
`/tmp/minimal-fast-shell-{1..4}.log`,
`/tmp/minimal-fast-shell-live.log`, and
`/tmp/production-fast-mouse-live.log`.
