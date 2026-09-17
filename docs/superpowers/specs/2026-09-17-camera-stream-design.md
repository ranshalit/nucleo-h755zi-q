# Loopback "Camera" MJPEG Stream — Design

Date: 2026-09-17
App: `apps/zephyr-hello-shell`

## Goal

Serve a short, looping video clip (converted from `~/Downloads/videoplayback.mp4`)
from the board over the network as an MJPEG stream that VLC can open directly
as a network stream, simulating a live camera feed. No new hardware
(USB/SD) is required; everything is baked into internal flash and served over
the existing validated Ethernet link.

## Why not USB (UVC) or SD card

- USB isochronous transfers (required for a real USB UVC webcam class) have a
  known, unresolved stall issue already noted in this repo (see
  `apps/zephyr-msc-demo/src/usb_msc.c` comments, referencing
  `docs/usb_debug.md` steps 1-39 and 44, for both USB host mode and the UAC2
  audio device demo). Avoiding USB isoc entirely sidesteps that bug.
- Internal flash is used instead of SD card storage to avoid depending on a
  physically-present SD card and untested SDMMC wiring; flash is proven and
  always available in this build.

## Chosen approach: multipart MJPEG over a dedicated HTTP-like TCP port

Considered:
1. **Multipart `multipart/x-mixed-replace` MJPEG stream (chosen).** Standard
   IP-camera protocol. VLC opens it directly as a network stream URL.
2. Single "snapshot" JPEG endpoint polled by a browser `<img>` tag — works in
   browsers, but VLC cannot auto-refresh a static image URL, failing the
   stated requirement.
3. Raw concatenated MJPEG byte stream with no multipart boundaries — less
   standard/reliable for VLC's demuxer than approach 1, no real benefit.

Approach 1 is used.

### Thread isolation (important design decision)

Zephyr's built-in HTTP server (`CONFIG_HTTP_SERVER`, used by the existing
board dashboard on port 80) runs a **single thread** that `poll()`s every
connected client's socket (`http_server_run()` in
`subsys/net/lib/http/http_server_core.c`). A dynamic-resource GET callback
that "loops forever" feeding frames — pacing itself with `k_sleep()` between
frames — runs *inside that same shared thread*, and would block the existing
LED/board-IO dashboard while a stream is active.

To avoid this, the camera stream is **not** implemented as a dynamic resource
on the existing port-80 HTTP server. Instead:

- A new dedicated thread (`K_THREAD_DEFINE`, own stack) opens its own raw
  `zsock_*` BSD-socket TCP listening socket on **port 8081**.
- It accepts one client connection at a time, hand-writes a minimal HTTP
  response line and `multipart/x-mixed-replace; boundary=frame` headers
  itself (no Zephyr HTTP server framework involved for this path), then loops
  forever: send `--frame` boundary + `Content-Type: image/jpeg` + frame bytes
  + `k_sleep()` for frame-rate pacing + advance to next frame (wrapping at the
  end of the clip).
- This thread's blocking/pacing never touches the port-80 HTTP server thread,
  so the existing dashboard (LED state, board I/O JSON, HTML/CSS/JS assets)
  stays fully responsive while a stream is active.
- VLC opens `http://192.168.55.2:8081/` (network stream) to view the loop.

## Frame data pipeline (offline, host-side)

1. `scripts/convert_video_to_mjpeg_frames.py` (new script) takes an input
   video (default `~/Downloads/videoplayback.mp4`), extracts a short segment
   with `ffmpeg`, downsamples/re-encodes to individual JPEG frames matching
   the "balanced" profile below, and emits a generated C source/header pair:
   - `apps/zephyr-hello-shell/src/camera_frames.c`
   - `apps/zephyr-hello-shell/src/camera_frames.h`
2. Each frame is stored as a `static const uint8_t camera_frame_NN[] = {...};`
   array, which the linker places in flash (`.rodata`), plus a generated
   lookup table `{ const uint8_t *data; size_t len; }` array and a frame
   count constant. No frame data is copied into RAM at build or run time.
3. The generated files are committed to the repository (like the existing
   `web_assets.c/.h` pattern already used for the dashboard's HTML/CSS/JS),
   so the firmware build is self-contained and reproducible without
   re-running `ffmpeg` at build time.

### Profile ("balanced", approved)

- Resolution: 320x240
- Frame rate: ~8 fps
- Clip length: ~6 seconds (~48 frames)
- Expected flash footprint: roughly 300-500 KB (to be confirmed once frames
  are generated; current firmware uses 27% of the 1 MB internal flash region,
  leaving ample headroom)

The conversion script validates the resulting total frame-data size against a
configurable flash budget and fails loudly (non-zero exit, clear error
message) if the budget is exceeded, so an oversized clip cannot silently
overflow flash.

## New/changed files

- `scripts/convert_video_to_mjpeg_frames.py` (new) — offline ffmpeg + codegen
  pipeline.
- `apps/zephyr-hello-shell/src/camera_frames.c` / `.h` (new, generated) —
  committed JPEG frame data + lookup table.
- `apps/zephyr-hello-shell/src/camera_stream.c` / `.h` (new) — dedicated
  thread, raw TCP listener on port 8081, minimal hand-written HTTP/multipart
  responder, frame pacing/looping logic.
- `apps/zephyr-hello-shell/src/main.c` — start the camera stream thread
  alongside the existing board I/O / HTTP server / USB host startup.
- `apps/zephyr-hello-shell/prj.conf` — any additional Kconfig needed for the
  extra socket/thread (e.g. raising `CONFIG_NET_SOCKETS` limits if needed).
- `apps/zephyr-hello-shell/src/web_assets.c` (existing, regenerated) — add a
  small link/label on the dashboard pointing at the stream URL for
  convenience (not required for VLC use, just a courtesy link).
- `README.md` — document the new stream URL and how to view it in VLC.

## Error handling

- Conversion script: fails with a clear error if `ffmpeg`/`ffprobe` are
  missing, if the input video is missing, or if encoded frame data exceeds
  the configured flash budget.
- Runtime: if the frame table is empty or corrupt (defensive check), the
  stream thread logs an error and refuses new connections rather than
  sending malformed data.
- Only one streaming client is served at a time (simple accept-loop
  design); a second concurrent connection attempt is accepted only after the
  first disconnects. This matches the "view in VLC" use case and keeps the
  implementation simple.
- Client disconnects are detected by a failed `send()` on the streaming
  socket; the thread closes the socket and returns to `accept()` for the next
  client without needing a restart.

## Testing / validation

- Build: confirm the app still builds for `nucleo_h755zi_q/stm32h755xx/m7`
  and check the `FLASH` region usage reported by the build stays under the
  configured budget.
- On hardware: flash the board, then use `ffplay http://192.168.55.2:8081/`
  or open the same URL as a network stream in VLC, and confirm continuous,
  seamless looping playback.
- Regression: confirm the existing dashboard (LED control, board status JSON,
  HTML/CSS/JS) on port 80 remains responsive (e.g. via `curl`) while the
  stream is actively being viewed, validating true thread isolation.
