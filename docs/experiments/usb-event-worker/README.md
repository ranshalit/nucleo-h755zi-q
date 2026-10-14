# USB FIFO event-worker experiment - 2026-10-14

## Outcome

The NUCLEO-H755ZI-Q application runs the directly attached mouse and microphone
without the normal host thread's continuous 50 us busy-wait. The experimental
default is a FIFO-notification-driven worker, with CPU idle sleep enabled.
The original polling path remains available for comparison and `usbhost pollmode`.

Two additional integration details were necessary:

1. Disable the unused USB2 ULPI sleep clock when using the embedded FS PHY.
2. Coalesce intermediate IRQ wake-ups and repeated unchanged HAL URB states.
   Queue meaningful state transitions, not every retry of the same state.

This does **not** move the entire HAL IRQ handler into a thread. Hardware IRQ
servicing, connection flags, SOF timer updates, and microphone completion/
resubmission remain in interrupt context. The FIFO preserves notification
snapshots in order; the worker then reads live HAL/middleware state.

## Setup and firmware

- Board: NUCLEO-H755ZI-Q, `nucleo_h755zi_q/stm32h755xx/m7`, 480 MHz.
- Mouse: Dell/PixArt `413c:301a`, directly attached to USER USB CN13.
- Microphone: Logitech H570e Stereo `046d:0a56`, UAC1, 48 kHz, stereo, 16-bit.
- Zephyr: `v4.2.2`, commit `dbb536326e6`; STM32 HAL module `1e753266`.
- Toolchain: official Zephyr SDK `arm-zephyr-eabi`, local SDK version `1.0.1`.
- Application baseline: `ba42e91`; isolated branch `feature/usb-event-worker`.
- UART: ST-LINK VCP, 115200 baud. Close minicom/other readers during captures.
- Ethernet: direct host link, board `192.168.55.2/24`, host
  `192.168.55.101/24`. The route must use the board-facing NIC, not Wi-Fi/TAP.

Final verified event HEX SHA-256:
`0d3fd2cb7004fcd9689990f9c423f4f6f693ff388809af1d54d07ad7a35c62c4`.
Polling comparison HEX SHA-256:
`418364128ed84a3e7c569205077442aac4ac8c1e6f044fbc3633a36f4a187671`.
Build outputs are not committed. These hashes identify the tested local images;
rebuilding with a different build timestamp may produce different hashes.

## Implementation

`usb_host_events.c` owns a 128-entry `k_msgq`, a binary wake semaphore, and
latency/queue counters protected by a spinlock. IRQ producers never block.
Port events and changed URB states enter the FIFO. A new transfer resets that
pipe's notification state, so two successive transfers completing with the
same URB state still produce two notifications. Repeated unchanged states
and SOF/intermediate IRQ activity wake the worker without filling the FIFO.

The single USB worker keeps its original priority 10. It processes notifications
sequentially, taking eight bounded middleware steps per notification and another
eight for tick/deadline work. It then blocks on the semaphore, with a 10 ms
recovery timeout when no wake arrives. Normal operation is IRQ/SOF-driven, not a
20 kHz thread loop. The periodic SOF event still provides endpoint scheduling
and timeout progress; this is not an exclusively transfer-completion-driven stack.

`usbhost status` prints mode, CPU sleep configuration, FIFO counters, maximum
enqueue-to-consumption latency, wake/step counts, and thread runtime cycles.
`usbhost workerstats` starts a fresh counter interval without discarding queued
events or erasing overflow evidence. Startup delays inflate lifetime latency,
so operational measurements clear counters after enumeration.

FIFO overflow is an explicit fatal experimental error: print the count/type,
deinitialize the active class, stop the host, and require a board reset.
There is no hidden fallback to spinning. The diagnostic `usbhost pollmode on`
intentionally uses the original polling path because it disables the NVIC IRQ;
normal event mode is `usbhost pollmode off`.

## Why the sleep-clock change matters

With the initial FIFO worker and unchanged clock setup, the mouse failed control
transfers during enumeration. SOF progress stopped (`timer=152` in the recorded
trial), although Ethernet and HTTP still worked. Keeping the exact same FIFO
worker but preventing CPU WFI/WFE made enumeration and live mouse input work.
That diagnostic did not save power: the idle thread spun instead.

The existing Zephyr device drivers already contain the relevant STM32H7 USB2
rule in `drivers/usb/device/usb_dc_stm32.c` and `drivers/usb/udc/udc_stm32.c`:
the USB2 embedded FS PHY needs the ULPI clock disabled in sleep mode.
Our HAL host adapter had not applied it. The original sleep register read was
`RCC_AHB1LPENR=0x1e03c023`; clearing `USB2OTGHSULPILPEN` gives `0x0e03c023`.
The change uses the existing LL API:

```c
LL_AHB1_GRP1_DisableClockSleep(LL_AHB1_GRP1_PERIPH_USB2OTGHSULPI);
```

With this change and normal CPU sleep restored, mouse enumeration and input
worked. These controlled trials show the busy loop was masking a CPU-sleep/
PHY-clock integration problem; they do not explain every historical camera
failure or prove a universal electrical cause for TXERR.

## Why blindly queueing every IRQ failed

The first microphone trials filled the FIFO with generic IRQ notifications.
After generic IRQ wake-ups were coalesced, repeated control-transfer URB
notifications still overflowed it. HAL's control NAK/retry handling can report
the same live URB state repeatedly, including while ST middleware is inside
blocking enumeration delays.

The final implementation queues changes once per pipe/transfer and uses
coalesced immediate wake-ups for repeats. It does not enlarge the FIFO to hide
the storm and does not discard meaningful changed-state notifications silently.
The failed trials and final successful trials are both retained below.

## Hardware results

| Trial | Result | Evidence |
| --- | --- | --- |
| Original polling mouse | Working reports, 543 to 748 in captured interval | `logs/mouse-poll-trial1.log` |
| FIFO, original sleep-clock setup | Enumeration failed; no overflow | `logs/mouse-event-trial1.log` |
| FIFO, CPU sleep disabled | Working movement/buttons; 799 to 1,784 reports | `logs/mouse-event-no-idle-live.log` |
| FIFO, sleep-clock fix, normal sleep | Three fresh resets passed before notification deduplication; movement/buttons worked | `logs/mouse-event-fixed-reset*.log`, `logs/mouse-event-fixed-live.log` |
| Initial microphone FIFO | Explicit overflow/host abort | `logs/mic-event-overflow-diagnostic.log` |
| Intermediate IRQ wake-ups coalesced only | Repeated URB notifications still overflowed | `logs/mic-event-coalesced-reset1.log` |
| Final microphone worker | Three fresh resets passed; packets advancing; zero errors/missed frames/overflow | `logs/mic-event-final-reset*.log` |
| Final microphone with stream and API load | HTTP 200; ~192 KB/s PCM; speech running, zero recognition gaps/drops; zero USB errors/missed frames/overflow | `logs/mic-event-final-stream-load.log`, `logs/mic-event-final-stream-uart.log`, `logs/mic-event-final-api-load.log` |
| Final mouse reconnect/live input | 770 to 1,852 reports; 297 button-bearing UART lines; no transfer-error messages or overflow | `logs/mouse-event-final-reconnect-live.log` |
| Final mouse resets | Three fresh resets reached mouse class | `logs/mouse-event-final-reset*.log` |
| Final microphone reconnect | Ready capture, packets 24,816 to 36,843; speech running, zero errors/missed frames/overflow | `logs/mic-event-final-reconnect.log` |

The final microphone stream transferred 3,846,144 PCM bytes over 20.014 seconds:
192,172.8 B/s. The polling comparator transferred 2,883,584 bytes over 15.003
seconds: 192,201.7 B/s. Audio content was drained in memory, not saved.

USB thread-attributed runtime was calculated from two UART snapshots:

```text
percentage = delta_worker_cycles / (delta_uptime_ms * 480000) * 100
```

- Microphone polling baseline: **71.50%** over 22.297 s.
- Microphone event worker: **8.62%** over 22.297 s.
- Final event worker with audio stream/API load: **8.60%** over 22.286 s.
- Final stationary mouse: **2.89%** over 5.071 s.
- Final live mouse: **20.16%** over 22.268 s, including heavy per-report UART output.

These are **thread-attributed clock-time estimates**, not whole-system CPU or
power measurements. Interrupt time may be attributed to whichever thread was
running. Inference, networking, UART logging, and interrupt activity still use CPU.

Operational FIFO high-water reached 70/128 in the deduplicated microphone
trial, with up to ~70 ms latency while higher-priority work ran. Audio stayed
healthy because its same-frame resubmission remained in the ISR. The final
stream-load trial reached 65/128 and ~65 ms. Stationary final mouse trials
usually observed ~100–115 us, but one included a ~48 ms maximum. There is no
hard worker-latency guarantee from these observations.

The microphone PCM ring's `drops` count grows when no player drains it; that is
expected bounded-ring eviction, not USB packet errors or speech queue loss.
Stream connection also intentionally discards old ring contents to start live.
Both baseline and event variants show this. Recognition `gaps`/`drops` and
USB `errors`/`missed` stayed zero in the successful recorded intervals.

Host ping and dashboard/API HTTP 200 were verified. Audio streaming and repeated
mic/speech API requests ran concurrently. Browser listening quality, a new
labeled speech-accuracy benchmark, and camera streaming were not retested.

## Reproduce

Use a separate build directory for each variant and the existing shared west
workspace. From the repository/worktree root:

```bash
export ZEPHYR_BASE=/path/to/shared/zephyrproject/zephyr
scripts/flash_zephyr_hello_shell.sh --build-only --build-dir /tmp/usb-event
```

The script selects the official Zephyr ARM SDK. For the polling comparator:

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/path/to/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-
west build -b nucleo_h755zi_q/stm32h755xx/m7 \
  "$PWD/apps/zephyr-hello-shell" -d /tmp/usb-poll -- \
  -DEXTRA_CONF_FILE="$PWD/apps/zephyr-hello-shell/usb-poll-baseline.conf" \
  -DTOOLCHAIN_HAS_PICOLIBC=ON \
  -DCONFIG_TOOLCHAIN_CROSS_COMPILE_SUPPORTS_THREAD_LOCAL_STORAGE=y
```

Configuration overlays:

- `usb-poll-baseline.conf`: original 50 us worker, original sleep-clock setup.
- `usb-original-sleep-clock.conf`: FIFO worker without the PHY sleep-clock fix.
- `usb-no-idle-sleep.conf`: FIFO worker, original clock setup, CPU sleep disabled.
- No overlay: final FIFO worker, sleep-clock fix enabled, normal CPU sleep.

Flash with the verified script, then capture UART:

```bash
scripts/flash_zephyr_hello_shell.sh --flash-only --skip-validate \
  --build-dir /tmp/usb-event
python3 scripts/capture_usb_worker_trial.py \
  --port /dev/serial/by-id/YOUR-STLINK-VCP \
  --log /tmp/mic-event.log --duration 20 --mode event --device microphone
```

Use `--device mouse --require-input` while moving/clicking to require measured
report growth. Add `--reset --programmer /path/to/STM32_Programmer_CLI` for a
fresh hardware reset captured from boot. The capture command checks final host
class state, event-mode overflow, microphone readiness/packet progress/error
growth, and optional mouse report growth. It exits nonzero on failure.

Native Zephyr kernel tests:

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 \
  apps/zephyr-hello-shell/tests/usb_host/events -d /tmp/usb-event-tests
/tmp/usb-event-tests/zephyr/zephyr.exe
```

Nine tests cover FIFO ordering/snapshots, early/late wake-ups, timeout, IRQ
enqueue, SOF/intermediate wake coalescing, overflow visibility, measurement
reset without dropping events, and repeated URB state handling across transfers.
Existing enumeration, mouse selection/decoding, audio descriptor/buffer, and
speech host regression tests also passed.

## Evidence and limitations

`logs/` contains sanitized UART and HTTP observations from successful and failed
stages, not just the final outcome. ANSI terminal escapes and trailing whitespace
are stripped. UART
`printf` and shell output can interleave during mouse activity; original line
fragments are retained rather than inventing reconstructed output. Some early
captures may also overlap a user minicom session; final microphone comparisons
were run after it was closed.

No credentials, host serial paths, ST-LINK identifiers, or microphone PCM
recordings are included. Device VID/PID, zero-valued device serial descriptors,
local lab IP addresses, classifier labels/counters, and register values remain
as reproduction evidence.

This experiment validates only the two directly attached devices above under
the recorded workloads, without a hub or external protocol analyzer. It is not
a guarantee against arbitrary worker starvation. Queue overflow still stops
the experimental host rather than silently losing state transitions.
