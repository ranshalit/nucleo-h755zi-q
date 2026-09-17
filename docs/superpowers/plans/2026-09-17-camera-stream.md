# Loopback MJPEG Camera Stream Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serve a short, looping video clip from `apps/zephyr-hello-shell` as a
`multipart/x-mixed-replace` MJPEG stream on a dedicated TCP port, so VLC (or
`ffplay`) can open `http://192.168.55.2:8081/` and view it like a live camera
feed, without disturbing the existing port-80 dashboard.

**Architecture:** An offline Python + ffmpeg pipeline converts a source video
into a small number of JPEG frames and generates a committed C source/header
pair holding those frames as `const uint8_t[]` arrays (flash/`.rodata`, no RAM
copy). A pure-logic C module formats the MJPEG multipart HTTP headers and
frame-index looping, independent of any networking code, and is tested
natively with `gcc` (no Zephyr build required). A thin Zephyr module wires
that logic to a dedicated `k_thread` that owns its own raw BSD-socket TCP
listener on port 8081, fully isolated from the Zephyr HTTP server thread that
runs the existing dashboard on port 80.

**Tech Stack:** Zephyr RTOS 4.2.2 (`nucleo_h755zi_q/stm32h755xx/m7`), Zephyr
POSIX sockets (`zephyr/net/socket.h`), C99, Python 3.10 + ffmpeg (host-side,
offline), gcc (native unit tests, same pattern as
`apps/zephyr-hello-shell/tests/usb_host/test_usb_enumerator.c`).

## Global Constraints

- Board static IPv4 is `192.168.55.2` (from `apps/zephyr-hello-shell/prj.conf`
  `CONFIG_NET_CONFIG_MY_IPV4_ADDR`); do not change this.
- Existing port-80 HTTP dashboard (`http_server.c`) must remain fully
  responsive while the camera stream is active — the two features must not
  share a thread.
- Total encoded frame data must not exceed 512 KiB (524288 bytes) of internal
  flash; current firmware already uses ~27% of the 1 MB `FLASH` region.
- Target profile: 320x240, ~8 fps, ~6 second loop (~48 frames).
- USB isochronous transfers must not be used (known unresolved stall bug
  documented in `apps/zephyr-msc-demo/src/usb_msc.c`); this feature is
  Ethernet/HTTP-only.
- Follow existing repo conventions: `SPDX-License-Identifier: Apache-2.0`
  header on all new files, `k_thread_create` + `K_THREAD_STACK_DEFINE` for new
  threads (not `K_THREAD_DEFINE`), native gcc tests live under
  `apps/zephyr-hello-shell/tests/<subsystem>/` with a matching
  `scripts/run_<subsystem>_tests.sh` runner.
- Toolchain for building the Zephyr app: `ZEPHYR_TOOLCHAIN_VARIANT=cross-compile`,
  `CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-`
  (see `README.md`).

---

### Task 1: Pure-logic MJPEG multipart formatting module

**Files:**
- Create: `apps/zephyr-hello-shell/src/mjpeg_multipart.h`
- Create: `apps/zephyr-hello-shell/src/mjpeg_multipart.c`
- Test: `apps/zephyr-hello-shell/tests/camera/test_mjpeg_multipart.c`
- Create: `scripts/run_mjpeg_multipart_tests.sh`

**Interfaces:**
- Consumes: nothing (pure logic, no Zephyr/socket dependencies).
- Produces:
  - `int mjpeg_format_stream_header(char *buf, size_t buf_len)` — writes the
    HTTP response line + multipart `Content-Type` header into `buf`, returns
    bytes written (excluding NUL) or `-1` if it doesn't fit.
  - `int mjpeg_format_part_header(char *buf, size_t buf_len, size_t frame_len)`
    — writes one multipart part header (boundary + `Content-Type: image/jpeg`
    + `Content-Length: <frame_len>` + blank line) into `buf`, returns bytes
    written (excluding NUL) or `-1` if it doesn't fit.
  - `size_t mjpeg_next_frame_index(size_t current_index, size_t frame_count)`
    — returns the next frame index, wrapping to `0` after `frame_count - 1`.
  - `#define MJPEG_BOUNDARY "frame"`.
  These are consumed by Task 4 (`camera_stream.c`).

- [ ] **Step 1: Write the failing test**

Create `apps/zephyr-hello-shell/tests/camera/test_mjpeg_multipart.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mjpeg_multipart.h"

static void test_stream_header_contains_boundary(void)
{
	char buf[256];
	int len = mjpeg_format_stream_header(buf, sizeof(buf));

	assert(len > 0);
	assert((size_t)len == strlen(buf));
	assert(strstr(buf, "multipart/x-mixed-replace; boundary=frame") != NULL);
	assert(strstr(buf, "HTTP/1.1 200 OK") != NULL);
	printf("PASS: test_stream_header_contains_boundary\n");
}

static void test_stream_header_too_small_buffer(void)
{
	char buf[8];
	int len = mjpeg_format_stream_header(buf, sizeof(buf));

	assert(len == -1);
	printf("PASS: test_stream_header_too_small_buffer\n");
}

static void test_part_header_contains_content_length(void)
{
	char buf[256];
	int len = mjpeg_format_part_header(buf, sizeof(buf), 12345);

	assert(len > 0);
	assert((size_t)len == strlen(buf));
	assert(strstr(buf, "--frame\r\n") != NULL);
	assert(strstr(buf, "Content-Type: image/jpeg\r\n") != NULL);
	assert(strstr(buf, "Content-Length: 12345\r\n") != NULL);
	printf("PASS: test_part_header_contains_content_length\n");
}

static void test_part_header_too_small_buffer(void)
{
	char buf[8];
	int len = mjpeg_format_part_header(buf, sizeof(buf), 42);

	assert(len == -1);
	printf("PASS: test_part_header_too_small_buffer\n");
}

static void test_next_frame_index_wraps(void)
{
	assert(mjpeg_next_frame_index(0, 3) == 1);
	assert(mjpeg_next_frame_index(1, 3) == 2);
	assert(mjpeg_next_frame_index(2, 3) == 0);
	printf("PASS: test_next_frame_index_wraps\n");
}

static void test_next_frame_index_single_frame(void)
{
	assert(mjpeg_next_frame_index(0, 1) == 0);
	printf("PASS: test_next_frame_index_single_frame\n");
}

int main(void)
{
	test_stream_header_contains_boundary();
	test_stream_header_too_small_buffer();
	test_part_header_contains_content_length();
	test_part_header_too_small_buffer();
	test_next_frame_index_wraps();
	test_next_frame_index_single_frame();
	printf("ALL TESTS PASSED\n");
	return 0;
}
```

Create `apps/zephyr-hello-shell/src/mjpeg_multipart.h` (declarations only, no
implementation yet, so the test fails to link):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MJPEG_MULTIPART_H_
#define MJPEG_MULTIPART_H_

#include <stddef.h>

#define MJPEG_BOUNDARY "frame"

/* Formats the initial HTTP response line + multipart content-type header
 * into buf. Returns the number of bytes written (excluding the terminating
 * NUL), or -1 if buf_len is too small to hold the header. */
int mjpeg_format_stream_header(char *buf, size_t buf_len);

/* Formats one multipart part header (boundary marker + Content-Type +
 * Content-Length + blank line) for a frame of the given length into buf.
 * Returns the number of bytes written (excluding the terminating NUL), or
 * -1 if buf_len is too small. */
int mjpeg_format_part_header(char *buf, size_t buf_len, size_t frame_len);

/* Returns the next frame index to send, wrapping back to 0 once
 * current_index reaches frame_count - 1. frame_count must be > 0. */
size_t mjpeg_next_frame_index(size_t current_index, size_t frame_count);

#endif /* MJPEG_MULTIPART_H_ */
```

Create an empty `apps/zephyr-hello-shell/src/mjpeg_multipart.c` with just the
include, so the build fails at link time rather than compile time:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "mjpeg_multipart.h"
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
gcc -std=c99 -Wall -Wextra -Werror \
  -I/home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src \
  /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/tests/camera/test_mjpeg_multipart.c \
  /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/mjpeg_multipart.c \
  -o /tmp/test_mjpeg_multipart
```
Expected: FAIL at link time with `undefined reference to 'mjpeg_format_stream_header'`
(and similarly for the other two functions).

- [ ] **Step 3: Write minimal implementation**

Replace `apps/zephyr-hello-shell/src/mjpeg_multipart.c` with:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "mjpeg_multipart.h"

#include <stdio.h>

int mjpeg_format_stream_header(char *buf, size_t buf_len)
{
	int written = snprintf(buf, buf_len,
			       "HTTP/1.1 200 OK\r\n"
			       "Content-Type: multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY "\r\n"
			       "Cache-Control: no-cache\r\n"
			       "Connection: close\r\n"
			       "\r\n");

	if (written < 0 || (size_t)written >= buf_len) {
		return -1;
	}

	return written;
}

int mjpeg_format_part_header(char *buf, size_t buf_len, size_t frame_len)
{
	int written = snprintf(buf, buf_len,
			       "--" MJPEG_BOUNDARY "\r\n"
			       "Content-Type: image/jpeg\r\n"
			       "Content-Length: %zu\r\n"
			       "\r\n",
			       frame_len);

	if (written < 0 || (size_t)written >= buf_len) {
		return -1;
	}

	return written;
}

size_t mjpeg_next_frame_index(size_t current_index, size_t frame_count)
{
	size_t next = current_index + 1;

	if (next >= frame_count) {
		return 0;
	}

	return next;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
gcc -std=c99 -Wall -Wextra -Werror \
  -I/home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src \
  /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/tests/camera/test_mjpeg_multipart.c \
  /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/mjpeg_multipart.c \
  -o /tmp/test_mjpeg_multipart && /tmp/test_mjpeg_multipart
```
Expected:
```
PASS: test_stream_header_contains_boundary
PASS: test_stream_header_too_small_buffer
PASS: test_part_header_contains_content_length
PASS: test_part_header_too_small_buffer
PASS: test_next_frame_index_wraps
PASS: test_next_frame_index_single_frame
ALL TESTS PASSED
```

- [ ] **Step 5: Add the test runner script**

Create `scripts/run_mjpeg_multipart_tests.sh` (mirrors
`scripts/run_usb_enumerator_tests.sh`):

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/src"
TEST_DIR="${REPO_ROOT}/apps/zephyr-hello-shell/tests/camera"
BUILD_DIR="${ZEPHYR_HOST_TEST_BUILD_DIR:-/tmp/mjpeg-multipart-test}"

mkdir -p "${BUILD_DIR}"

gcc -std=c99 -Wall -Wextra -Werror \
  -I"${SRC_DIR}" \
  "${TEST_DIR}/test_mjpeg_multipart.c" \
  "${SRC_DIR}/mjpeg_multipart.c" \
  -o "${BUILD_DIR}/test_mjpeg_multipart"

"${BUILD_DIR}/test_mjpeg_multipart"
```

Run:
```bash
chmod +x /home/ranshal/project/stm32h7/scripts/run_mjpeg_multipart_tests.sh
/home/ranshal/project/stm32h7/scripts/run_mjpeg_multipart_tests.sh
```
Expected: same `ALL TESTS PASSED` output as Step 4.

- [ ] **Step 6: Commit**

```bash
cd /home/ranshal/project/stm32h7
git add apps/zephyr-hello-shell/src/mjpeg_multipart.h \
        apps/zephyr-hello-shell/src/mjpeg_multipart.c \
        apps/zephyr-hello-shell/tests/camera/test_mjpeg_multipart.c \
        scripts/run_mjpeg_multipart_tests.sh
git commit -m "Add pure-logic MJPEG multipart header formatting module"
```

---

### Task 2: Offline video-to-JPEG-frames conversion script

**Files:**
- Create: `scripts/convert_video_to_mjpeg_frames.py`
- Test: `scripts/test_convert_video_to_mjpeg_frames.sh`

**Interfaces:**
- Consumes: nothing from prior tasks (standalone host-side tool).
- Produces: a `camera_frames.c` / `camera_frames.h` pair (written into
  whatever `--output-dir` is passed) with:
  - `struct camera_frame { const uint8_t *data; size_t len; };`
  - `extern const struct camera_frame camera_frames[];`
  - `extern const size_t camera_frame_count;`
  Task 3 runs this script against the real input video to produce the
  committed frame data consumed by Task 4 (`camera_stream.c`).

- [ ] **Step 1: Write the failing test**

Create `scripts/test_convert_video_to_mjpeg_frames.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONVERTER="${SCRIPT_DIR}/convert_video_to_mjpeg_frames.py"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "==> generating synthetic test input video"
ffmpeg -y -f lavfi -i testsrc=size=64x64:rate=10 -t 2 -pix_fmt yuv420p \
  "${WORK_DIR}/test-input.mp4" >/dev/null 2>&1

echo "==> running converter with a generous flash budget (expect success)"
OUT_DIR="${WORK_DIR}/out-ok"
mkdir -p "${OUT_DIR}"
OUTPUT="$(python3 "${CONVERTER}" --input "${WORK_DIR}/test-input.mp4" \
  --output-dir "${OUT_DIR}" --width 32 --height 24 --fps 4 --duration 1 \
  --var-prefix test_frame --flash-budget-bytes 1000000)"
echo "${OUTPUT}"

EXPECTED_FRAME_COUNT="$(echo "${OUTPUT}" | grep -oE 'Generated [0-9]+ frames' | grep -oE '[0-9]+')"

[[ -f "${OUT_DIR}/camera_frames.c" ]] || { echo "FAIL: camera_frames.c missing"; exit 1; }
[[ -f "${OUT_DIR}/camera_frames.h" ]] || { echo "FAIL: camera_frames.h missing"; exit 1; }

echo "==> checking generated source compiles"
gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only \
  -I"${OUT_DIR}" "${OUT_DIR}/camera_frames.c"

echo "==> checking generated frame count matches converter output"
cat > "${WORK_DIR}/count_main.c" <<'MAIN_EOF'
#include <stdio.h>
#include "camera_frames.h"

int main(void)
{
	printf("%zu\n", camera_frame_count);
	return 0;
}
MAIN_EOF

gcc -std=c99 -Wall -Wextra -Werror -I"${OUT_DIR}" \
  "${WORK_DIR}/count_main.c" "${OUT_DIR}/camera_frames.c" \
  -o "${WORK_DIR}/count_main"
ACTUAL_FRAME_COUNT="$("${WORK_DIR}/count_main")"

if [[ "${ACTUAL_FRAME_COUNT}" != "${EXPECTED_FRAME_COUNT}" ]]; then
  echo "FAIL: camera_frame_count (${ACTUAL_FRAME_COUNT}) != converter-reported count (${EXPECTED_FRAME_COUNT})"
  exit 1
fi

echo "==> running converter with a tiny flash budget (expect failure)"
OUT_DIR_FAIL="${WORK_DIR}/out-fail"
mkdir -p "${OUT_DIR_FAIL}"
if python3 "${CONVERTER}" --input "${WORK_DIR}/test-input.mp4" \
  --output-dir "${OUT_DIR_FAIL}" --width 32 --height 24 --fps 4 --duration 1 \
  --var-prefix test_frame --flash-budget-bytes 10 2>"${WORK_DIR}/stderr.txt"; then
  echo "FAIL: converter should have exited non-zero for an impossible flash budget"
  exit 1
fi

grep -q "exceeds flash budget" "${WORK_DIR}/stderr.txt" || {
  echo "FAIL: expected 'exceeds flash budget' error message"
  cat "${WORK_DIR}/stderr.txt"
  exit 1
}

echo "ALL TESTS PASSED"
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
chmod +x /home/ranshal/project/stm32h7/scripts/test_convert_video_to_mjpeg_frames.sh
/home/ranshal/project/stm32h7/scripts/test_convert_video_to_mjpeg_frames.sh
```
Expected: FAIL — `python3: can't open file '.../convert_video_to_mjpeg_frames.py': [Errno 2] No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `scripts/convert_video_to_mjpeg_frames.py`:

```python
#!/usr/bin/env python3
"""Convert a video clip into a C array of JPEG frames for MJPEG streaming.

Extracts a short segment of an input video with ffmpeg, downsamples/encodes
it to individual JPEG frames, and emits a C source/header pair containing
the frame bytes as `static const uint8_t` arrays plus a lookup table, so the
frames can be linked directly into firmware flash (no RAM copy needed).
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a video clip into a C array of JPEG frames."
    )
    parser.add_argument(
        "--input",
        default=str(Path.home() / "Downloads" / "videoplayback.mp4"),
        help="Input video file (default: ~/Downloads/videoplayback.mp4).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Directory to write camera_frames.c and camera_frames.h into.",
    )
    parser.add_argument("--width", type=int, default=320, help="Output frame width.")
    parser.add_argument("--height", type=int, default=240, help="Output frame height.")
    parser.add_argument("--fps", type=float, default=8.0, help="Output frame rate.")
    parser.add_argument(
        "--duration", type=float, default=6.0, help="Clip length in seconds."
    )
    parser.add_argument(
        "--start", type=float, default=0.0, help="Start offset into the input video."
    )
    parser.add_argument(
        "--jpeg-quality",
        type=int,
        default=6,
        help="ffmpeg -q:v value, 2 (best) - 31 (worst). Default: 6.",
    )
    parser.add_argument(
        "--flash-budget-bytes",
        type=int,
        default=512 * 1024,
        help="Fail if total encoded frame data exceeds this many bytes.",
    )
    parser.add_argument(
        "--var-prefix",
        default="camera_frame",
        help="Prefix used for generated per-frame C array names.",
    )
    return parser.parse_args()


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        print(f"error: required tool '{name}' not found on PATH", file=sys.stderr)
        sys.exit(1)


def extract_frames(args: argparse.Namespace, tmpdir: Path) -> list[Path]:
    input_path = Path(args.input).expanduser()
    if not input_path.is_file():
        print(f"error: input video not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    pattern = tmpdir / "frame_%04d.jpg"
    cmd = [
        "ffmpeg",
        "-y",
        "-ss",
        str(args.start),
        "-t",
        str(args.duration),
        "-i",
        str(input_path),
        "-vf",
        f"fps={args.fps},scale={args.width}:{args.height}",
        "-q:v",
        str(args.jpeg_quality),
        str(pattern),
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("error: ffmpeg failed to extract frames", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(1)

    frames = sorted(tmpdir.glob("frame_*.jpg"))
    if not frames:
        print("error: ffmpeg produced no frames", file=sys.stderr)
        sys.exit(1)

    return frames


def check_flash_budget(frames: list[Path], budget_bytes: int) -> int:
    total = sum(frame.stat().st_size for frame in frames)
    if total > budget_bytes:
        print(
            f"error: encoded frame data is {total} bytes, exceeds flash budget of "
            f"{budget_bytes} bytes",
            file=sys.stderr,
        )
        sys.exit(1)
    return total


def write_frame_array(out, var_name: str, data: bytes) -> None:
    out.write(f"static const uint8_t {var_name}[] = {{\n")
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        line = ", ".join(f"0x{byte:02x}" for byte in chunk)
        out.write(f"\t{line},\n")
    out.write("};\n\n")


def generate_sources(
    frames: list[Path], var_prefix: str, output_dir: Path
) -> None:
    header_path = output_dir / "camera_frames.h"
    source_path = output_dir / "camera_frames.c"

    header_path.write_text(
        "/*\n"
        " * SPDX-License-Identifier: Apache-2.0\n"
        " *\n"
        " * Generated by scripts/convert_video_to_mjpeg_frames.py. Do not edit by hand.\n"
        " */\n\n"
        "#ifndef ZEPHYR_HELLO_SHELL_CAMERA_FRAMES_H_\n"
        "#define ZEPHYR_HELLO_SHELL_CAMERA_FRAMES_H_\n\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "struct camera_frame {\n"
        "\tconst uint8_t *data;\n"
        "\tsize_t len;\n"
        "};\n\n"
        "extern const struct camera_frame camera_frames[];\n"
        "extern const size_t camera_frame_count;\n\n"
        "#endif /* ZEPHYR_HELLO_SHELL_CAMERA_FRAMES_H_ */\n"
    )

    with source_path.open("w") as out:
        out.write(
            "/*\n"
            " * SPDX-License-Identifier: Apache-2.0\n"
            " *\n"
            " * Generated by scripts/convert_video_to_mjpeg_frames.py. Do not edit by hand.\n"
            " */\n\n"
            '#include "camera_frames.h"\n\n'
        )

        var_names = []
        for index, frame in enumerate(frames):
            var_name = f"{var_prefix}_{index:04d}"
            var_names.append(var_name)
            write_frame_array(out, var_name, frame.read_bytes())

        out.write("const struct camera_frame camera_frames[] = {\n")
        for var_name in var_names:
            out.write(f"\t{{ {var_name}, sizeof({var_name}) }},\n")
        out.write("};\n\n")

        out.write(
            f"const size_t camera_frame_count = {len(var_names)};\n"
        )


def main() -> None:
    args = parse_args()

    require_tool("ffmpeg")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        frames = extract_frames(args, tmpdir)
        total_bytes = check_flash_budget(frames, args.flash_budget_bytes)
        generate_sources(frames, args.var_prefix, output_dir)

    print(f"Generated {len(frames)} frames, {total_bytes} bytes total")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
/home/ranshal/project/stm32h7/scripts/test_convert_video_to_mjpeg_frames.sh
```
Expected:
```
==> generating synthetic test input video
==> running converter with a generous flash budget (expect success)
Generated 4 frames, 2801 bytes total
==> checking generated source compiles
==> checking generated frame count matches converter output
==> running converter with a tiny flash budget (expect failure)
ALL TESTS PASSED
```
(exact byte/frame counts may vary slightly by ffmpeg version, that's fine —
the test only checks internal consistency, not exact values)

- [ ] **Step 5: Commit**

```bash
cd /home/ranshal/project/stm32h7
chmod +x scripts/convert_video_to_mjpeg_frames.py
git add scripts/convert_video_to_mjpeg_frames.py scripts/test_convert_video_to_mjpeg_frames.sh
git commit -m "Add offline ffmpeg pipeline to convert video into JPEG frame C arrays"
```

---

### Task 3: Generate committed camera_frames.c/.h from the real source video

**Files:**
- Create (generated, committed): `apps/zephyr-hello-shell/src/camera_frames.c`
- Create (generated, committed): `apps/zephyr-hello-shell/src/camera_frames.h`

**Interfaces:**
- Consumes: `scripts/convert_video_to_mjpeg_frames.py` from Task 2.
- Produces: `camera_frames[]` array and `camera_frame_count` (as defined in
  Task 2), consumed by Task 4 (`camera_stream.c`).

- [ ] **Step 1: Run the converter against the real input video**

Run (uses the "balanced" profile approved during design: 320x240, 8 fps, 6s):
```bash
python3 /home/ranshal/project/stm32h7/scripts/convert_video_to_mjpeg_frames.py \
  --input ~/Downloads/videoplayback.mp4 \
  --output-dir /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src \
  --width 320 --height 240 --fps 8 --duration 6 --start 0 \
  --jpeg-quality 6 --flash-budget-bytes 524288 --var-prefix camera_frame
```
Expected: a line like `Generated 48 frames, <N> bytes total` where `<N>` is
well under `524288`. If it fails with `exceeds flash budget`, re-run with a
higher `--jpeg-quality` number (worse quality, smaller size) or shorter
`--duration` until it fits — do not raise `--flash-budget-bytes` above `524288`
without re-checking the Global Constraints section above.

- [ ] **Step 2: Verify the generated files look correct**

Run:
```bash
head -20 /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/camera_frames.h
tail -5 /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/camera_frames.c
wc -c /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/camera_frames.c
```
Expected: the header declares `struct camera_frame`, `camera_frames[]`, and
`camera_frame_count`; the source ends with the `camera_frames[]` table and
`const size_t camera_frame_count = 48;` (or whatever count was generated).

- [ ] **Step 3: Compile-check the generated file standalone**

Run:
```bash
gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only \
  -I/home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src \
  /home/ranshal/project/stm32h7/apps/zephyr-hello-shell/src/camera_frames.c
```
Expected: no output, exit code 0.

- [ ] **Step 4: Commit**

```bash
cd /home/ranshal/project/stm32h7
git add apps/zephyr-hello-shell/src/camera_frames.c apps/zephyr-hello-shell/src/camera_frames.h
git commit -m "Generate looping camera frame data from videoplayback.mp4"
```

---

### Task 4: Camera stream thread (dedicated port 8081, isolated from dashboard)

**Files:**
- Create: `apps/zephyr-hello-shell/src/camera_stream.h`
- Create: `apps/zephyr-hello-shell/src/camera_stream.c`
- Modify: `apps/zephyr-hello-shell/src/main.c`
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - From Task 1: `mjpeg_format_stream_header()`, `mjpeg_format_part_header()`,
    `mjpeg_next_frame_index()` (`mjpeg_multipart.h`).
  - From Task 3: `struct camera_frame { const uint8_t *data; size_t len; };`,
    `camera_frames[]`, `camera_frame_count` (`camera_frames.h`).
  - From Zephyr: `zephyr/kernel.h`, `zephyr/net/socket.h`,
    `zephyr/logging/log.h`.
- Produces:
  - `int camera_stream_start(void)` — starts the dedicated streaming thread;
    idempotent (safe to call once at boot, matching `board_http_server_start`
    convention in `main.c`).
  - `uint16_t camera_stream_port(void)` — returns `8081`.
  These are consumed by `main.c`.

- [ ] **Step 1: Create the header**

Create `apps/zephyr-hello-shell/src/camera_stream.h`:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_
#define ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_

#include <stdint.h>

int camera_stream_start(void);
uint16_t camera_stream_port(void);

#endif /* ZEPHYR_HELLO_SHELL_CAMERA_STREAM_H_ */
```

- [ ] **Step 2: Implement the streaming thread**

Create `apps/zephyr-hello-shell/src/camera_stream.c`:

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serves a looping MJPEG (multipart/x-mixed-replace) stream of the frames
 * in camera_frames.c on a dedicated TCP port and dedicated thread, isolated
 * from the Zephyr HTTP server thread that runs the port-80 dashboard
 * (http_server.c). This keeps the dashboard responsive while a client (e.g.
 * VLC) is actively viewing the stream.
 */

#include "camera_stream.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "camera_frames.h"
#include "mjpeg_multipart.h"

LOG_MODULE_REGISTER(camera_stream, LOG_LEVEL_INF);

#define CAMERA_STREAM_PORT 8081
#define CAMERA_STREAM_THREAD_STACK_SIZE 4096
#define CAMERA_STREAM_THREAD_PRIORITY 7
#define CAMERA_STREAM_FRAME_INTERVAL_MS 125 /* ~8 fps */
#define CAMERA_STREAM_HEADER_BUF_SIZE 128

K_THREAD_STACK_DEFINE(camera_stream_thread_stack, CAMERA_STREAM_THREAD_STACK_SIZE);
static struct k_thread camera_stream_thread_data;
static bool camera_stream_started;

static ssize_t camera_stream_send_all(int sock, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;

	while (len > 0) {
		ssize_t sent = zsock_send(sock, ptr, len, 0);

		if (sent < 0) {
			return sent;
		}

		ptr += (size_t)sent;
		len -= (size_t)sent;
	}

	return 0;
}

static int camera_stream_serve_client(int client_sock)
{
	char header_buf[CAMERA_STREAM_HEADER_BUF_SIZE];
	int header_len;
	size_t frame_index = 0;

	header_len = mjpeg_format_stream_header(header_buf, sizeof(header_buf));
	if (header_len < 0) {
		LOG_ERR("Stream header does not fit in buffer");
		return -ENOMEM;
	}

	if (camera_stream_send_all(client_sock, header_buf, (size_t)header_len) < 0) {
		return -errno;
	}

	while (true) {
		const struct camera_frame *frame = &camera_frames[frame_index];
		int part_header_len = mjpeg_format_part_header(header_buf, sizeof(header_buf),
								frame->len);

		if (part_header_len < 0) {
			LOG_ERR("Part header does not fit in buffer");
			return -ENOMEM;
		}

		if (camera_stream_send_all(client_sock, header_buf, (size_t)part_header_len) < 0) {
			return -errno;
		}

		if (camera_stream_send_all(client_sock, frame->data, frame->len) < 0) {
			return -errno;
		}

		if (camera_stream_send_all(client_sock, "\r\n", 2) < 0) {
			return -errno;
		}

		frame_index = mjpeg_next_frame_index(frame_index, camera_frame_count);
		k_sleep(K_MSEC(CAMERA_STREAM_FRAME_INTERVAL_MS));
	}
}

static void camera_stream_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int server_sock;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(CAMERA_STREAM_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (camera_frame_count == 0) {
		LOG_ERR("No camera frames available, stream thread exiting");
		return;
	}

	server_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_sock < 0) {
		LOG_ERR("Failed to create stream socket: %d", errno);
		return;
	}

	if (zsock_bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Failed to bind stream socket to port %d: %d", CAMERA_STREAM_PORT, errno);
		zsock_close(server_sock);
		return;
	}

	if (zsock_listen(server_sock, 1) < 0) {
		LOG_ERR("Failed to listen on stream socket: %d", errno);
		zsock_close(server_sock);
		return;
	}

	LOG_INF("Camera stream listening on port %d", CAMERA_STREAM_PORT);

	while (true) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client_sock = zsock_accept(server_sock, (struct sockaddr *)&client_addr,
					       &client_addr_len);

		if (client_sock < 0) {
			LOG_ERR("Failed to accept stream client: %d", errno);
			continue;
		}

		LOG_INF("Camera stream client connected");
		camera_stream_serve_client(client_sock);
		LOG_INF("Camera stream client disconnected");
		zsock_close(client_sock);
	}
}

int camera_stream_start(void)
{
	if (camera_stream_started) {
		return 0;
	}

	k_tid_t tid = k_thread_create(&camera_stream_thread_data, camera_stream_thread_stack,
				      K_THREAD_STACK_SIZEOF(camera_stream_thread_stack),
				      camera_stream_thread_entry, NULL, NULL, NULL,
				      CAMERA_STREAM_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "camera_stream");
	camera_stream_started = true;

	return 0;
}

uint16_t camera_stream_port(void)
{
	return CAMERA_STREAM_PORT;
}
```

- [ ] **Step 3: Wire the sources into the build**

Modify `apps/zephyr-hello-shell/CMakeLists.txt` — in the `target_sources(app
PRIVATE ...)` block, add the three new files after `src/web_assets.c`:

```cmake
target_sources(app PRIVATE
  src/boot_optimizations.c
  src/main.c
  src/board_io.c
  src/http_server.c
  src/web_assets.c
  src/mjpeg_multipart.c
  src/camera_frames.c
  src/camera_stream.c
  src/usb_host/stm32_hcd_adapter.c
  src/usb_host/usb_gpio_capture.c
  src/usb_host/usb_host_shell.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_core.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_ctlreq.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_ioreq.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_pipes.c
)
```

- [ ] **Step 4: Start the stream thread from main.c**

Modify `apps/zephyr-hello-shell/src/main.c` — add the include near the top:

```c
#include "board_io.h"
#include "camera_stream.h"
#include "http_server.h"
```

Then in `main(void)`, after the existing `board_usb_host_start()` block (the
last block before `return 0;`), add:

```c
	ret = board_usb_host_start();
	if (ret != 0) {
		printf("USB host startup failed: %d\n", ret);
		/* Non-fatal: continue running LEDs/HTTP even if USB host init failed. */
	}

	ret = camera_stream_start();
	if (ret != 0) {
		printf("Camera stream startup failed: %d\n", ret);
		/* Non-fatal: continue running LEDs/HTTP even if the stream failed. */
	} else {
		printf("Camera stream listening on http://%s:%u/\n",
		       CONFIG_NET_CONFIG_MY_IPV4_ADDR, camera_stream_port());
	}

	return 0;
}
```

- [ ] **Step 5: Build the firmware**

Run:
```bash
cd /home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-
west build -p always -b nucleo_h755zi_q/stm32h755xx/m7 \
  ../../../apps/zephyr-hello-shell -d /tmp/stm32h7-zephyr-hello-shell
```
Expected: build succeeds, ending with a `Memory region ... FLASH ... RAM ...`
summary table and no errors. Confirm `FLASH` usage is still comfortably under
100% (it was 27% before this feature; expect it to grow by roughly the
generated frame data size from Task 3, e.g. a few hundred KB).

- [ ] **Step 6: Commit**

```bash
cd /home/ranshal/project/stm32h7
git add apps/zephyr-hello-shell/src/camera_stream.h \
        apps/zephyr-hello-shell/src/camera_stream.c \
        apps/zephyr-hello-shell/src/main.c \
        apps/zephyr-hello-shell/CMakeLists.txt
git commit -m "Serve looping MJPEG camera stream on dedicated port 8081"
```

---

### Task 5: Dashboard link and README documentation

**Files:**
- Modify: `apps/zephyr-hello-shell/src/web_assets.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: nothing new (documentation/UI-only change).
- Produces: nothing consumed by other tasks (final, user-facing task).

- [ ] **Step 1: Add a stream link to the dashboard hero section**

Modify `apps/zephyr-hello-shell/src/web_assets.c` — in
`board_http_index_html[]`, change:

```c
    "      <div class=\"hero-actions\">\n"
    "        <span id=\"live-indicator\" class=\"badge\" data-tone=\"muted\">starting</span>\n"
    "        <button id=\"refresh-button\" class=\"primary-button\" type=\"button\">Refresh now</button>\n"
    "        <p id=\"last-updated\" class=\"timestamp\">Waiting for first update...</p>\n"
    "      </div>\n"
```

to:

```c
    "      <div class=\"hero-actions\">\n"
    "        <span id=\"live-indicator\" class=\"badge\" data-tone=\"muted\">starting</span>\n"
    "        <button id=\"refresh-button\" class=\"primary-button\" type=\"button\">Refresh now</button>\n"
    "        <p id=\"last-updated\" class=\"timestamp\">Waiting for first update...</p>\n"
    "        <p class=\"timestamp\"><a href=\"http://192.168.55.2:8081/\">Live camera stream (open in VLC)</a></p>\n"
    "      </div>\n"
```

- [ ] **Step 2: Rebuild to confirm the asset change compiles**

Run:
```bash
cd /home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/home/ranshal/.local/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-
west build -p always -b nucleo_h755zi_q/stm32h755xx/m7 \
  ../../../apps/zephyr-hello-shell -d /tmp/stm32h7-zephyr-hello-shell
```
Expected: build succeeds as in Task 4 Step 5.

- [ ] **Step 3: Document the feature in README.md**

Modify `README.md` — after the "### HTTP validation" section (right before
the line `## STM32CubeIDE reference build`), add a new subsection:

```markdown
### Camera stream (looping MJPEG)

The board also serves a short, looping video clip as a `multipart/x-mixed-replace`
MJPEG stream on a dedicated TCP port, isolated from the port-80 HTTP
dashboard so viewing the stream never blocks LED/board-IO requests:

- stream URL: `http://192.168.55.2:8081/`

Open that URL directly in VLC (`Media > Open Network Stream`) or with
`ffplay http://192.168.55.2:8081/` to view the loop. A convenience link is
also shown on the dashboard (`http://192.168.55.2/`).

The frame data comes from `apps/zephyr-hello-shell/src/camera_frames.c`,
generated offline from a source video with:

```bash
python3 scripts/convert_video_to_mjpeg_frames.py \
  --input ~/Downloads/videoplayback.mp4 \
  --output-dir apps/zephyr-hello-shell/src \
  --width 320 --height 240 --fps 8 --duration 6 --start 0 \
  --jpeg-quality 6 --flash-budget-bytes 524288 --var-prefix camera_frame
```

Re-run this command (and rebuild/reflash) to change the clip. The script
fails with a clear error if the encoded frame data would exceed the
`--flash-budget-bytes` limit.
```

- [ ] **Step 4: Commit**

```bash
cd /home/ranshal/project/stm32h7
git add apps/zephyr-hello-shell/src/web_assets.c README.md
git commit -m "Document and link the looping MJPEG camera stream"
```

---

### Task 6: Flash to hardware and validate end-to-end

**Files:**
- None (validation-only task; no new files).

**Interfaces:**
- Consumes: the fully built firmware image from `/tmp/stm32h7-zephyr-hello-shell`
  produced by Task 4/5.
- Produces: nothing (terminal validation task).

- [ ] **Step 1: Flash the board**

Run:
```bash
export PATH="$HOME/.local/bin:$PATH:/opt/st/stm32cubeide_2.1.1/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin:$PATH"
cd /home/ranshal/project/stm32h7/vendor/zephyrproject/zephyr
west flash -d /tmp/stm32h7-zephyr-hello-shell --runner stm32cubeprogrammer
```
Expected: output ends with `Start operation achieved successfully`.

- [ ] **Step 2: Confirm serial boot log shows the stream starting**

Connect to `/dev/ttyACM0` at `115200 8N1` (e.g. `screen /dev/ttyACM0 115200`
or `minicom`) and reset the board. Expected lines (order may vary slightly):
```text
HTTP server listening on http://192.168.55.2:80/
Camera stream listening on http://192.168.55.2:8081/
```

- [ ] **Step 3: Validate the stream is reachable and looks like an MJPEG stream**

Run (from the host, on the `192.168.55.0/24` link):
```bash
curl --max-time 3 -sD - http://192.168.55.2:8081/ -o /tmp/camera_stream_sample.bin
head -c 200 /tmp/camera_stream_sample.bin
```
Expected: response headers include
`Content-Type: multipart/x-mixed-replace; boundary=frame`, and the captured
bytes start with `--frame` followed by a `Content-Type: image/jpeg` part
header and JPEG magic bytes (`\xff\xd8\xff`).

- [ ] **Step 4: View the stream in VLC / ffplay**

Run:
```bash
ffplay http://192.168.55.2:8081/
```
or open `http://192.168.55.2:8081/` in VLC via `Media > Open Network Stream`.
Expected: a continuously looping video window with no visible stalls between
loop iterations.

- [ ] **Step 5: Confirm the dashboard stays responsive during streaming**

While `ffplay`/VLC from Step 4 is still connected and playing, run from
another terminal:
```bash
curl --fail --max-time 3 http://192.168.55.2/api/v1/status
curl --fail --max-time 3 http://192.168.55.2/api/v1/io
```
Expected: both commands return promptly (well under the 3s timeout) with
valid JSON, confirming the port-80 dashboard thread was not blocked by the
port-8081 streaming thread.

- [ ] **Step 6: Run all native unit tests one more time as a final regression check**

Run:
```bash
/home/ranshal/project/stm32h7/scripts/run_mjpeg_multipart_tests.sh
/home/ranshal/project/stm32h7/scripts/test_convert_video_to_mjpeg_frames.sh
/home/ranshal/project/stm32h7/scripts/run_usb_enumerator_tests.sh
```
Expected: all three print their respective `ALL TESTS PASSED` (or equivalent)
final line with exit code 0.
