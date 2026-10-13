# USB microphone browser listening demo

## Goal and scope

Capture live microphone audio from a Logitech H570e Stereo headset
(USB VID:PID `046d:0a56`) connected to the NUCLEO-H755ZI-Q USER USB CN13
host port and play it through Microsoft Edge on the Ethernet-connected
computer. Speech should be audible with roughly less than one second of
end-to-end delay. Add a microphone card to the existing board dashboard,
including Start/Stop listening, a live input-level indicator, and explicit
device/stream health. Only one browser listens at a time.

The same firmware must continue to support the existing boot mouse when it is
plugged in instead of the headset. Detect which device is attached at runtime
and recover when the user unplugs one and plugs in the other. A hub and
simultaneous mouse/headset use are out of scope. The headset's speaker output,
browser-to-headset audio, recording, downloads, and other audio devices are
out of scope. Keep the capture/transport boundaries suitable for a future
two-way audio extension without implementing that extension now.

Implement and document the feature on branch `feature/usb-mic-web-demo` in
`.worktrees/usb-mic-web-demo`; do not change the primary `master` checkout or
its untracked documents.

## USB host and capture

The existing STM32 USB host currently registers only the boot-mouse class and
chooses a class using the first interface. Extend class registration and
selection to distinguish a boot mouse from the headset's audio function,
including a composite device whose first interface is not the microphone
stream. Preserve boot-mouse enumeration, polling, snapshots and dashboard
behavior. Do not attempt to use Zephyr's USB *device* UAC2 loopback example
as a USB *host* microphone driver.

Before selecting a USB audio format, read the H570e's actual configuration
and class-specific descriptors on the board. Identify the audio-input
streaming interface and its alternate settings, endpoint, USB Audio Class
version, sample format, channel count, supported rate(s), synchronization
requirements and packet size. The imported STM32 host parser records a
limited set of interfaces and alternates, so retain and safely validate the
raw configuration descriptor where required; reject truncated, malformed or
unsupported layouts. Select a supported PCM input setting from the
advertised capabilities, issue the required class-specific controls and
`SET_INTERFACE`, then receive isochronous IN packets using the existing
STM32 HCD/host pump. No assumed headset format is accepted without descriptor
and on-board transfer confirmation.

A focused capture module owns the selected interface/pipe, its transfer
state, a bounded PCM ring buffer, and counters for received packets, missing
packets and dropped samples. It publishes format, attachment state and
measured input level through a small snapshot API. USB polling must not wait
for a browser or network send. A disconnected microphone stops capture,
invalidates buffered audio and resets playback session state; mouse hot-plug
still works after switching back.

## HTTP transport and dashboard

Keep port 80's existing status, I/O, LED, mouse and camera handling intact.
Expose microphone status at `GET /api/v1/mic`: attachment, ready state,
selected sample rate/channels/bit depth, normalized input level, drop/error
counters and a reason when the device is unsupported. Do not treat
unsupported devices as ready microphones. Run the single-client PCM stream
on its own TCP listening port
(8082), following the camera stream's separate-thread pattern so a long-lived
listener cannot occupy the dashboard HTTP worker. Send a fixed 16-byte binary
header (`MIC1`, then little-endian 32-bit sample rate, 16-bit channel count,
16-bit bits per sample and 32-bit session identifier), followed by
ordered, signed little-endian interleaved PCM frames over an HTTP response.
The first version supports the headset's verified advertised integer PCM
format; unsupported bit depth or other encodings fail explicitly. Edge
fetches this cross-origin port with `Access-Control-Allow-Origin: *` on this
credential-free GET endpoint. Start streaming only when capture has a
supported format. Return HTTP 503 for absent or unsupported microphone and
409 when another listener is active, with a readable reason.
Bound socket waits and buffers, promptly release the client on stop/unplug,
and handle a slow client by dropping old audio rather than blocking USB
capture or growing latency without limit. Do not fabricate silence to conceal
capture failure.

Add a USB mic card alongside the mouse card in the existing dark dashboard.
It shows attachment, selected format, live level and stream/drop/error state.
Start listening is an explicit user click to satisfy Edge autoplay rules;
Stop closes the fetch stream and audio context. The client parses format
headers and incomplete network reads, converts supported PCM into Web Audio
buffers, and maintains a short bounded jitter queue. If it falls behind,
discard stale audio to return to near-live playback. Report unsupported
browser/audio format, busy, network failure or unplug in the card; reset the
audio queue on reconnect or format change. The mouse card stays functional
when the mouse is plugged in.

## Verification and acceptance

Use focused tests for USB descriptor parsing and mouse/audio selection,
malformed/unsupported formats, ring-buffer overflow and disconnect reset,
stream framing and busy/error responses, plus browser-side chunk parsing,
bounded playback queue and Start/Stop UI state. Build the board target and
native_sim target; give native_sim an explicit simulated or unavailable mic
state so its existing dashboard and tests still work. Preserve and run
existing mouse and web tests. The current worktree's baseline web tests do
not start because `puppeteer-core` is missing; resolve that dependency when
running implementation validation, without silently interpreting those
failures as product regressions.

On the physical board, inspect the connected H570e descriptors, confirm
reliable isochronous samples and a changing speech level, then flash and open
the dashboard in Edge. Acceptance requires intelligible spoken audio from
the board-attached headset in the browser with approximately less than one
second delay; no host-computer microphone is used. Confirm dashboard and
camera remain responsive during playback, a second listener is refused,
unplug/replug recovers cleanly, and plugging the boot mouse back in restores
its existing interaction. Document the connection, browser, endpoint and
diagnostic procedure in `README.md`. If headset USB format or electrical
limits make the goal unattainable, report the measured blocker rather than
claiming a working stream.
