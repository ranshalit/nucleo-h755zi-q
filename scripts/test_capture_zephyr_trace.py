#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().with_name("capture_zephyr_trace.sh")
REPO = SCRIPT.parent.parent

FAKE_OPENOCD = r"""#!/usr/bin/env python3
import os
from pathlib import Path
import signal
import time

log = Path(os.environ["TEST_LOG"])
with log.open("a") as f:
    f.write("openocd " + " ".join(__import__("sys").argv[1:]) + "\n")
def stop(signum, frame):
    with log.open("a") as f:
        f.write("openocd stopped\n")
    raise SystemExit(0)
signal.signal(signal.SIGTERM, stop)
if os.environ.get("FAIL_OPENOCD"):
    print("probe unavailable", flush=True)
    raise SystemExit(2)
if os.environ.get("FAIL_EXAMINATION"):
    print("Error: [stm32h7x.cm7] Examination failed", flush=True)
print("Info : Listening on port 3333 for gdb connections", flush=True)
while True:
    time.sleep(0.05)
"""

FAKE_GDB = r"""#!/usr/bin/env python3
import os
from pathlib import Path
import sys
import struct

def log(text):
    with open(os.environ["TEST_LOG"], "a") as f:
        f.write(text + "\n")
if "-x" not in sys.argv:
    for index, value in enumerate(sys.argv):
        if value == "-ex":
            log(sys.argv[index + 1])
    raise SystemExit(0)
commands = Path(sys.argv[sys.argv.index("-x") + 1]).read_text()
pos = int(os.environ.get("TEST_POS", "128"))
skip = False
reset = False
for command in commands.splitlines():
    if command.startswith("set $trace_reset = "):
        reset = command.endswith("1")
    if command == "if $trace_reset":
        skip = not reset
        continue
    if command.startswith("if $trace_pos"):
        skip = 0 < pos <= 16384
        continue
    if command == "end":
        skip = False
        continue
    if skip:
        continue
    log(command)
    if command.startswith("shell sleep"):
        log("sleep " + str(float(command.split()[-1])))
    if command == "quit 1":
        print("error: invalid trace position", file=sys.stderr)
        raise SystemExit(1)
    if command.startswith("dump binary memory"):
        if os.environ.get("FAIL_DUMP"):
            print("memory read failed", file=sys.stderr)
            raise SystemExit(1)
        data = (struct.pack("<IHI20s", 123456, 0x11, 0x24000000, b"demo")
                if os.environ.get("TEST_VALID_CTF") else b"x" * pos)
        # GDB's dump command splits on whitespace, keeping filename quotes.
        Path(command.split()[3]).write_bytes(data)
"""


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="trace wrapper ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "calls"
        self.elf = self.root / "matching image.elf"
        self.elf.write_bytes(b"ELF fixture")
        self.output = self.root / "capture with spaces"
        configs = self.root / "openocd scripts"
        for name in ("interface/stlink-dap.cfg", "target/stm32h7x.cfg"):
            path = configs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for name, content in (("openocd", FAKE_OPENOCD), ("gdb", FAKE_GDB)):
            path = self.root / name
            path.write_text(content)
            path.chmod(0o755)
        # Keep preflight tests independent of any real debugger using port 3333.
        shim = self.root / "python3"
        shim.write_text(f"""#!{sys.executable}
import os, socket, sys
if sys.argv[1:] == ["-"]:
    class TestSocket(socket.socket):
        def bind(self, address):
            if os.environ.get("TEST_PORT_BUSY"):
                raise OSError("Address already in use")
            if os.environ.get("TEST_PORT_TIME_WAIT") and not self.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR):
                raise OSError("Address already in use")
            return super().bind((address[0], 0))
    socket.socket = TestSocket
    exec(sys.stdin.read())
else:
    os.execv({sys.executable!r}, [{sys.executable!r}] + sys.argv[1:])
""")
        shim.chmod(0o755)
        self.env = {
            **os.environ,
            "HOME": str(self.root),
            "STM32_OPENOCD": str(self.root / "openocd"),
            "STM32_OPENOCD_SCRIPTS": str(configs),
            "ZEPHYR_GDB": str(self.root / "gdb"),
            "TEST_LOG": str(self.log),
            "PATH": str(self.root) + os.pathsep + os.environ["PATH"],
        }

    def run_capture(self, *args, extra_env=None, defaults=True):
        argv = ["bash", str(SCRIPT)]
        if defaults:
            argv += ["--elf", str(self.elf), "--output", str(self.output)]
        return subprocess.run(
            argv + list(args), cwd=self.root,
            env={**self.env, **(extra_env or {})},
            capture_output=True, text=True, timeout=15,
        )

    def calls(self):
        return self.log.read_text() if self.log.exists() else ""

    def test_help_without_tools(self):
        result = self.run_capture("--help", defaults=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--reset", result.stdout)
        self.assertIn("--seconds", result.stdout)
        self.assertFalse(self.log.exists())

    def test_snapshot_and_cleanup(self):
        result = self.run_capture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "channel0_0").stat().st_size, 128)
        self.assertEqual(
            (self.output / "metadata").read_bytes(),
            (REPO / "vendor/zephyrproject/zephyr/subsys/tracing/ctf/tsdl/metadata").read_bytes(),
        )
        calls = self.calls()
        self.assertNotIn("monitor reset", calls)
        self.assertNotIn("sleep ", calls)
        self.assertIn("monitor halt", calls)
        self.assertLess(calls.index("monitor resume"), calls.index("detach"))
        self.assertIn("openocd stopped", calls)
        self.assertIn(str(self.output), result.stdout)

    def test_reset_wait_order(self):
        result = self.run_capture("--reset", "--seconds", "0.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertIn("set CONNECT_UNDER_RESET 1", calls)
        ordered = ["monitor reset halt", "monitor resume", "sleep 0.1",
                   "\nmonitor halt", "dump binary memory", "detach"]
        positions = [calls.index(item) for item in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_reset_default_wait(self):
        result = self.run_capture("--reset")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("sleep 20.0", self.calls())

    def test_invalid_arguments_before_probe(self):
        for args in [
            ["--seconds"], ["--elf"], ["--output"], ["--bogus"],
            ["--seconds", "5"], ["--reset", "--seconds", "-1"],
            ["--reset", "--seconds", "nan"], ["--reset", "--seconds", "inf"],
            ["--reset", "--seconds", "abc"],
        ]:
            with self.subTest(args=args):
                result = self.run_capture(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("error:", result.stderr)
                self.assertFalse(self.log.exists())

    def test_missing_elf_before_probe(self):
        self.elf.unlink()
        result = self.run_capture()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ELF", result.stderr)
        self.assertFalse(self.log.exists())

    def test_existing_output_preserved(self):
        self.output.mkdir()
        marker = self.output / "channel0_0"
        marker.write_bytes(b"keep")
        result = self.run_capture()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(marker.read_bytes(), b"keep")
        self.assertFalse(self.log.exists())

    def test_bad_positions_resume_board(self):
        for pos in ("0", "-1", "16385"):
            with self.subTest(pos=pos):
                output = self.root / ("bad-" + pos)
                result = self.run_capture("--output", str(output),
                                          extra_env={"TEST_POS": pos})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("position", result.stderr)
                self.assertIn("monitor resume\ndetach", self.calls())
                self.assertIn("openocd stopped", self.calls())
                self.assertNotIn("Capture saved:", result.stdout)

    def test_dump_failure_resume_board(self):
        result = self.run_capture(extra_env={"FAIL_DUMP": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("memory read failed", result.stderr)
        self.assertIn("monitor resume\ndetach", self.calls())
        self.assertIn("openocd stopped", self.calls())

    def test_openocd_failure_visible(self):
        result = self.run_capture(extra_env={"FAIL_OPENOCD": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("probe unavailable", result.stderr)
        self.assertNotIn("target extended-remote", self.calls())

    def test_existing_server_not_touched(self):
        result = self.run_capture(extra_env={"TEST_PORT_BUSY": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("port 3333", result.stderr)
        self.assertFalse(self.log.exists())

    def test_closed_connection_does_not_block_capture(self):
        result = self.run_capture(extra_env={"TEST_PORT_TIME_WAIT": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Capture saved:", result.stdout)

    def test_examination_failure_reports_reset_without_connecting(self):
        result = self.run_capture(extra_env={"FAIL_EXAMINATION": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Examination failed", result.stderr)
        self.assertIn("--reset", result.stderr)
        self.assertNotIn("target extended-remote", self.calls())
        self.assertNotIn("monitor reset", self.calls())
        self.assertIn("openocd stopped", self.calls())

    def test_missing_debugger_does_not_reset_board(self):
        result = self.run_capture("--reset", extra_env={
            "ZEPHYR_GDB": str(self.root / "missing"),
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("openocd ", self.calls())

    @unittest.skipUnless(shutil.which("babeltrace2"), "babeltrace2 not installed")
    def test_capture_directory_decodes_with_babeltrace(self):
        result = self.run_capture(extra_env={"TEST_VALID_CTF": "1", "TEST_POS": "30"})
        self.assertEqual(result.returncode, 0, result.stderr)
        decoded = subprocess.run(
            ["babeltrace2", str(self.output)], capture_output=True, text=True,
            timeout=10,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("thread_switched_in", decoded.stdout)
        self.assertIn("demo", decoded.stdout)

    def test_default_output_outside_build(self):
        result = self.run_capture("--elf", str(self.elf), defaults=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        captures = list((self.root / "zephyr-traces").glob("*/channel0_0"))
        self.assertEqual(len(captures), 1)


if __name__ == "__main__":
    unittest.main()
