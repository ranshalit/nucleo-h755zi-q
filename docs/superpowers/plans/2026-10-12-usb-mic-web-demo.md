# USB Microphone Browser Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hear the board-attached Logitech H570e microphone in Microsoft Edge with roughly less than one second of delay, without losing the existing boot-mouse demo.

**Architecture:** The STM32 USB host selects the attached mouse or headset at runtime. A headset-specific class parses the actual USB configuration, receives isochronous PCM into a bounded ring, and publishes a status snapshot. A dedicated, single-listener HTTP stream sends PCM to an Edge Web Audio player on the existing dashboard; it never blocks USB polling or the dashboard HTTP worker.

**Tech Stack:** Zephyr on `nucleo_h755zi_q/stm32h755xx/m7`, STM32 HAL HCD plus imported STM32 USBH Core, Zephyr sockets/HTTP, C11 host unit tests, browser JavaScript/Web Audio.

## Global Constraints

- Work only in `.worktrees/usb-mic-web-demo` on `feature/usb-mic-web-demo`; leave the primary `master` checkout and its untracked documents untouched.
- The USB microphone is Logitech H570e Stereo, VID:PID `046d:0a56`, on USER USB CN13; mouse and headset are alternatives on the one port, not simultaneous hub clients.
- Edge on the Ethernet-connected computer is the first browser target; one listening browser at a time; capture-only, no headset speaker output, recording, or browser-to-headset path.
- Existing port-80 dashboard, LEDs, Ethernet, mouse behavior and port-8081 camera stream remain functional.
- Use the headset's *measured* descriptors and transfer results, not an assumed USB Audio Class revision, sample rate or endpoint. Never silently substitute browser-computer microphone input or simulated audio for the board headset.
- The board/host network defaults remain `192.168.55.2/24` and `192.168.55.100/24`.
- Report unsupported devices, transfer failure and network failure explicitly; the acceptance target is intelligible speech with approximately less than one second latency.
- Every implementation commit must include `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- Worktree builds must use the shared Zephyr checkout explicitly: `ZEPHYR_BASE=/home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr` and a separate build directory (do not expect ignored `vendor/` to exist in the worktree).

## File map

- `apps/zephyr-hello-shell/src/usb_host/usb_audio_desc.{h,c}`: bounds-checked raw USB audio descriptor parsing and format selection, no Zephyr dependency.
- `apps/zephyr-hello-shell/src/usb_host/usb_audio_buffer.{h,c}`: bounded PCM/sample buffer and session/drop accounting, no sockets or HAL.
- `apps/zephyr-hello-shell/src/usb_host/usb_audio_capture.{h,c}`: STM32 USBH audio class, interface control and isochronous IN transfer state; read-only snapshot interface for HTTP.
- `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c`: raw configuration dump and audio/mouse class registration.
- `apps/zephyr-hello-shell/third_party/stm32-mw-usb-host/Core/Src/usbh_core.c`: scoped class-selection correction if the audio device is composite; preserve mouse selection.
- `apps/zephyr-hello-shell/src/audio_stream.{h,c}`: one-client port-8082 socket/HTTP PCM stream and error/busy replies, independent of USB.
- `apps/zephyr-hello-shell/src/audio_wire.{h,c}`: fixed 16-byte `MIC1` header encoder for host tests.
- `apps/zephyr-hello-shell/src/http_server.c`: `GET /api/v1/mic` status, without changing existing response formats.
- `apps/zephyr-hello-shell/src/web_assets.c`: dashboard card and playback UI.
- `apps/zephyr-hello-shell/src/usb_host/usb_host_native_sim.c`: explicit unavailable mic snapshot in simulator; no fake audio.
- `apps/zephyr-hello-shell/CMakeLists.txt` and `prj.conf`: add only required sources and socket capacity.
- `apps/zephyr-hello-shell/tests/{usb_host,web}/`: descriptor/buffer/wire/UI regression tests.
- `README.md`: user setup, HTTP endpoints and troubleshooting.

### Task 1: Capture actual H570e descriptor and host constraints

**Files:**
- Modify: `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c`
- Create: `apps/zephyr-hello-shell/tests/usb_host/fixtures/h570e_config.hex`
- Modify: `docs/superpowers/specs/2026-10-12-usb-mic-web-demo-design.md` only if measured facts require changing the approved format/scope.

**Interfaces:** Produce the full raw configuration descriptor fixture and an enumerated device descriptor; later tasks consume the fixture. `usbhost config` must reject a not-yet-available/truncated configuration rather than print stale bytes.

- [ ] **Step 1: Add a shell test point.** Add `cmd_usbhost_config` next to `cmd_usbhost_descriptor`, and register `SHELL_CMD(config, NULL, "Dump raw configuration descriptor", cmd_usbhost_config)` in `sub_usbhost`. Print the valid `wTotalLength` and `CfgDesc_Raw` as indexed hex bytes, 16 per line. Check `HOST_CHECK_CLASS` or later, `wTotalLength >= 9`, `wTotalLength <= sizeof(CfgDesc_Raw)` and header `bDescriptorType == 2` before printing. Pause the USB poller or copy under its synchronization policy before reading mutable bytes.
- [ ] **Step 2: Build in isolation.** Run `ZEPHYR_BASE=/home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr scripts/flash_zephyr_hello_shell.sh --build-only --build-dir /tmp/stm32h7-mic-plan-build --zephyr-base /home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr`. Expected: successful board build; if Zephyr SDK is not auto-selected, set `ZEPHYR_TOOLCHAIN_VARIANT=zephyr`.
- [ ] **Step 3: Probe hardware.** Flash this branch's image with the same explicit `--build-dir`/`--zephyr-base`; over the board's ST-LINK UART issue `usbhost descriptor`, `usbhost config`, `usbhost status`. Record exactly the returned bytes in `tests/usb_host/fixtures/h570e_config.hex`, inspect audio-control version, streaming alternate settings, audio IN endpoint and all advertised PCM frequencies/channel widths. Verify the observed VID:PID `046d:0a56` and configuration length. If enumeration fails before the configuration becomes available, stop and diagnose that first; do not invent a fixture.
- [ ] **Step 4: Commit the diagnostic tool and captured fixture.** Use `git add` with the two explicit paths and `git commit -m "test(usb): capture H570e USB descriptors" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`.

### Task 2: Parse actual USB audio function safely

**Files:**
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_audio_desc.h`
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_audio_desc.c`
- Create: `apps/zephyr-hello-shell/tests/usb_host/test_usb_audio_desc.c`
- Test: `apps/zephyr-hello-shell/tests/usb_host/fixtures/h570e_config.hex`

**Interfaces:** `int usb_audio_select_input(const uint8_t *raw, size_t length, struct usb_audio_format *out)` returns `0` only for a validated audio-streaming **IN** alternate setting supporting integer PCM; negative `-EINVAL` for malformed descriptors and `-ENOTSUP` for unsupported formats. `usb_audio_format` contains `uint8_t interface_number, alternate_setting, endpoint, channels, bits_per_sample, interval; uint16_t packet_bytes; uint32_t sample_rate_hz; uint16_t audio_class_bcd;`. Parsing does not rely on `CfgDesc.Itf_Desc[0]` or its eight-entry limit.

- [ ] **Step 1: Write a failing C test.** Load the Task 1 fixture as bytes, assert VID-independent audio-input selection with `endpoint & 0x80`, isochronous transfer type, nonzero `sample_rate_hz`, valid `packet_bytes` and one of the actually advertised alternate settings. Add synthetic negative descriptors: `bLength=0`, descriptor extends past `wTotalLength`, no audio IN endpoint, non-PCM format, and packet capacity below `ceil(rate / 1000) * channels * bytes_per_sample` for full-speed 1-ms frames (the peak packet, not average bytes/ms; 44.1 kHz stereo 16-bit needs 180 B). Assert `-EINVAL` vs `-ENOTSUP` as declared.
- [ ] **Step 2: Observe failure.** Run `cc -std=c11 -Wall -Wextra -Werror -Iapps/zephyr-hello-shell/src/usb_host apps/zephyr-hello-shell/tests/usb_host/test_usb_audio_desc.c apps/zephyr-hello-shell/src/usb_host/usb_audio_desc.c -o /tmp/test_usb_audio_desc && /tmp/test_usb_audio_desc`. Expected: compile/link failure until the interface exists, then failing assertions before parsing is complete.
- [ ] **Step 3: Implement raw descriptor walk.** Validate the configuration header and declared total length; advance only by descriptors with `bLength >= 2` inside the buffer. Track each interface number/alternate setting and class-specific type-I PCM descriptors relevant to the actual H570e revision; accept a valid audio-streaming IN endpoint only when it belongs to that alternate and meets the measured bandwidth and format bounds. Do not assume interface zero or treat speaker OUT as microphone IN. Return the selected format via `out` only on success.
- [ ] **Step 4: Re-run the same test and confirm PASS.** Add more fixture-based cases if the hardware exposes several viable rates; choose a verified rate with suitable network bandwidth. Commit parser, fixture tests and header with `git commit -m "feat(usb): parse H570e input format" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`.

### Task 3: Runtime mouse/audio selection and bounded capture

**Files:**
- Modify: `apps/zephyr-hello-shell/src/usb_host/usbh_conf.h`
- Modify: `apps/zephyr-hello-shell/src/usb_host/usb_host_shell.c`
- Modify: `apps/zephyr-hello-shell/third_party/stm32-mw-usb-host/Core/Src/usbh_core.c`
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_audio_buffer.{h,c}`
- Create: `apps/zephyr-hello-shell/src/usb_host/usb_audio_capture.{h,c}`
- Modify: `apps/zephyr-hello-shell/src/usb_host/usb_host_native_sim.c`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Test: `apps/zephyr-hello-shell/tests/usb_host/test_usb_audio_buffer.c`
- Test: `apps/zephyr-hello-shell/tests/usb_host/test_usb_boot_mouse.c`

**Interfaces:** `void usb_audio_snapshot_get(struct usb_audio_snapshot *out)` reports `{connected, ready, session, sample_rate_hz, channels, bits_per_sample, level, packets, drops, errors, reason}`. `size_t usb_audio_read(uint32_t session, uint8_t *out, size_t capacity)` returns only complete interleaved PCM frames from that session; disconnect increments session and discards the ring. Ring producer runs from the USB poll thread; consumer runs from the stream thread. Native_sim returns `ready=false, reason="usb host unavailable"`.

- [ ] **Step 1: Write failing ring/session tests.** Feed packet-aligned byte arrays; assert FIFO order, level update from nonzero samples, overflow discards oldest complete frames and increments drops, empty reads are zero, and a disconnect invalidates an old session/clears stale bytes. Build with `cc -std=c11 -Wall -Wextra -Werror -Iapps/zephyr-hello-shell/src/usb_host apps/zephyr-hello-shell/tests/usb_host/test_usb_audio_buffer.c apps/zephyr-hello-shell/src/usb_host/usb_audio_buffer.c -o /tmp/test_usb_audio_buffer && /tmp/test_usb_audio_buffer`; expected failing compile, then PASS.
- [ ] **Step 2: Implement a bounded ring and snapshot.** Use a fixed, frame-aligned capacity sized to a few hundred milliseconds of the measured PCM format; no allocation in the 1-ms USB loop. Synchronize producer/consumer with Zephyr primitives or short critical sections while keeping socket send out of the lock. Preserve report counters across short reads and increment session on every disconnect or new attachment.
- [ ] **Step 3: Prove class selection.** Raise `USBH_MAX_NUM_SUPPORTED_CLASS` from 1 to 2, register boot mouse plus audio, and adapt `HOST_CHECK_CLASS` to scan actual interface descriptors or validated raw configuration for a supported function instead of matching only `Itf_Desc[0]`. Never index `pClass` past `ClassNumber`, never select speaker OUT alone, and keep the old boot-mouse device selecting `usb_boot_mouse_class`. Add a focused test using the H570e fixture and a synthetic boot-mouse config; run both and verify selection before connecting a real headset.
- [ ] **Step 4: Implement `usb_audio_capture` as an STM32 USBH class.** On Init parse Task 2 format and allocate/open an isochronous IN pipe; on Requests configure the advertised input control/clock as needed and `SET_INTERFACE` to the selected alternate; on BackgroundProcess submit frame-sized IN URBs, check completion/short packets/timeout/overrun, append only valid PCM frames to the ring, retry bounded failures and surface terminal errors. DeInit must close/free the pipe, reset the ring and mark disconnected. Confirm any USBH core/HAL isochronous scheduling limits on hardware before declaring the capture ready.
- [ ] **Step 5: Build and verify physical capture.** Use the Task 1 build command. Flash the board, see `connected/ready`, increasing packet count and nonzero level when speaking; confirm no counterfeit data when quiet or unplugged, then swap back to mouse and run the existing mouse shell/dashboard checks. Run existing C tests with their checked-in compiler procedure; fix only regressions from this task. Commit the reviewed changes with `git commit -m "feat(usb): capture headset microphone" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`.

### Task 4: Wire header, single-client audio stream and status API

**Files:**
- Create: `apps/zephyr-hello-shell/src/audio_wire.{h,c}`
- Create: `apps/zephyr-hello-shell/src/audio_stream.{h,c}`
- Create: `apps/zephyr-hello-shell/tests/usb_host/test_audio_wire.c`
- Modify: `apps/zephyr-hello-shell/src/http_server.c`
- Modify: `apps/zephyr-hello-shell/src/main.c`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Modify: `apps/zephyr-hello-shell/prj.conf`

**Interfaces:** `int audio_wire_header(uint8_t out[16], uint32_t rate, uint16_t channels, uint16_t bits, uint32_t session)` encodes bytes 0-3 `MIC1`, bytes 4-7 rate LE, 8-9 channels LE, 10-11 bits LE, 12-15 session LE. `int audio_stream_start(void)` starts a dedicated port-8082 listener. `GET /api/v1/mic` returns attachment, ready, format, level, packets, drops, errors and reason.

- [ ] **Step 1: Write the failing framing test.** For `(16000, 1, 16, 0x12345678)`, assert the exact 16 bytes `{0x4d,0x49,0x43,0x31,0x80,0x3e,0,0,1,0,16,0,0x78,0x56,0x34,0x12}`. Run `cc -std=c11 -Wall -Wextra -Werror -Iapps/zephyr-hello-shell/src apps/zephyr-hello-shell/tests/usb_host/test_audio_wire.c apps/zephyr-hello-shell/src/audio_wire.c -o /tmp/test_audio_wire && /tmp/test_audio_wire`; expected failure before implementation, then PASS.
- [ ] **Step 2: Add header encoder and socket stream.** Adapt `camera_stream.c`'s separate listener, bounded send timeouts and close-on-error. Parse only `GET /` with bounded HTTP request line/headers. Return `200` with `Content-Type: application/octet-stream`, `Cache-Control: no-store`, `Access-Control-Allow-Origin: *` and the 16-byte header when ready; `503` with a plain reason when absent/unsupported, `409` when occupied; also send CORS on error responses. Send ordered frame-aligned reads from `usb_audio_read`, discard stale session/buffered bytes, and close on detach, timeout, malformed request or client disconnect; never block the USB poller. Ensure the listener remains responsive to a second connection while one client streams (nonblocking accept/poll or a separate bounded worker).
- [ ] **Step 3: Add `/api/v1/mic` in `http_server.c`.** Follow `mouse_handler`/`append_to_buffer`; report format numbers and `ready=false` with an explicit reason for unavailable USB in native_sim. Size JSON defensively and send 500 on formatting overflow, not truncated 200. Start `audio_stream_start()` from `main.c` only after board/network setup. Account for the listener and up to two simultaneous sockets in Zephyr fd/context pools; preserve camera plus dashboard headroom.
- [ ] **Step 4: Verify transport and coexistence.** Build both `nucleo_h755zi_q/stm32h755xx/m7` and `native_sim/native/64` using the shared Zephyr base and separate build dirs. On hardware, check `curl -i http://192.168.55.2/api/v1/mic`; with capture ready, `curl --max-time 2 --output /tmp/h570e-test.pcm http://192.168.55.2:8082/` must yield `MIC1` followed by changing PCM; a simultaneous second curl must get 409 promptly. Check port 80 and port 8081 still respond. Commit with `git commit -m "feat(net): stream microphone PCM" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`.

### Task 5: Edge playback UI and browser behavior

**Files:**
- Modify: `apps/zephyr-hello-shell/src/web_assets.c`
- Create: `apps/zephyr-hello-shell/tests/web/test_mic_player.cjs`

**Interfaces:** Mic card IDs `mic-state`, `mic-level`, `mic-format`, `mic-errors`, `mic-toggle`; player consumes `GET /api/v1/mic` and `http://<dashboard-host>:8082/`. Keep mouse polling independent. `MIC1` body is 16-byte header followed by frame-aligned PCM; handle arbitrary fetch chunk splits.

- [ ] **Step 1: Write the failing browser test.** Use the existing `test_camera_toggle.cjs` Puppeteer pattern and a local fake HTTP mic stream. Assert disconnected card and disabled listening initially; then feed a split `MIC1` header, fragmented nonzero PCM, `503`, `409`, and mid-stream disconnect. Assert card text/state and Stop closes the request. For queue timing, make a pure JS helper so a Node unit test can assert clipping the buffered-ahead duration below 1 second.
- [ ] **Step 2: Fix baseline browser dependency without changing product dependencies.** Existing browser tests require `puppeteer-core`; use a locally available module via `NODE_PATH` if present, or install in a temporary test location and set `NODE_PATH` there. Do not commit `node_modules` or assume the literal Google Chrome path in old tests exists on Edge-only systems. Run `node apps/zephyr-hello-shell/tests/web/test_mic_player.cjs` and confirm the new assertions fail against the old page.
- [ ] **Step 3: Implement the mic card and playback.** On explicit Start, create/resume `AudioContext`, fetch the port-8082 stream with `AbortController`, parse the exact 16-byte header across chunks, decode the device-advertised signed little-endian PCM format, and schedule short buffers at the correct rate/channel count. Keep audio queued only a small bounded duration; on lag discard stale samples. Stop/abort/stream end releases audio resources and shows an explicit error or disconnected state. Poll status/level separately without disrupting existing mouse/LED polling or camera toggle. Do not call `getUserMedia()`.
- [ ] **Step 4: Run the fake-stream test plus old web checks.** Confirm a second browser gets busy, click Stop allows another listener, and mouse/camera cards still work. On the physical board, hear spoken audio in Edge, measure spoken-to-playback delay against the roughly one-second goal and verify no audio after unplug. Commit with `git commit -m "feat(web): listen to board USB microphone" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`.

### Task 6: Hardware acceptance and user documentation

**Files:**
- Modify: `README.md`
- Modify: `scripts/flash_zephyr_hello_shell.sh` only for checks that can run reliably without requiring headset audio playback.

**Interfaces:** Document `http://192.168.55.2/`, `GET /api/v1/mic`, and `http://192.168.55.2:8082/`; include CN13 and host Ethernet connection, Edge Start/Stop, audio output selection, hot-swap, errors and the one-listener limit.

- [ ] **Step 1: Document measured H570e format and the accepted USB class/interface/endpoint from Task 1.** Include the UART diagnostic command, support limits, `/api/v1/mic` JSON example with real values from the board, stream header layout and a way to confirm that capture originated on the board. Explain that speaker playback through the headset and hubs are not included.
- [ ] **Step 2: Run focused tests then the two build targets.** Re-run `test_usb_audio_desc`, `test_usb_audio_buffer`, `test_audio_wire`, `test_mic_player.cjs`, existing boot-mouse and web tests, and separate board/native_sim builds. If a test cannot run (missing Edge/Chrome or board), report the exact limitation rather than marking it passed.
- [ ] **Step 3: Validate with headset, mouse and network concurrently.** On board: H570e attach → mic level changes → Edge spoken playback at approximately under one second → second listener 409 → mouse hot-swap restores mouse dashboard → headset replug restarts fresh session. Throughout, check `/api/v1/io`/`/api/v1/status` and the camera stream while listening. Inspect packet drops and buffer/CPU pressure; if latency or format goal fails, diagnose and report measured cause, do not report success.
- [ ] **Step 4: Commit documentation (and reliable script checks if added)** with `git commit -m "docs: demonstrate USB mic in Edge" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"`; verify `git status --short --branch` in both worktree and primary checkout.
