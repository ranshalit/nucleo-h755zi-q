#!/usr/bin/env python3
"""Check quiet startup and runtime logging against a native_sim app binary."""

import os
import pty
import re
import select
import socket
import subprocess
import time
import unittest


class QuietBootTest(unittest.TestCase):
    def test_quiet_boot_and_runtime_filters(self):
        executable = os.environ["ZEPHYR_QUIET_BOOT_EXE"]
        master, slave = pty.openpty()
        process = subprocess.Popen(
            [executable, "-uart_stdinout"],
            stdin=slave, stdout=slave, stderr=subprocess.PIPE
        )
        os.close(slave)

        def read_for(seconds):
            output = bytearray()
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    output.extend(os.read(master, 4096))
            return re.sub(
                r"\x1b\[[0-?]*[ -/]*[@-~]", "", output.decode(errors="replace")
            )

        def command(text):
            os.write(master, (text + "\n").encode())
            return read_for(0.5)

        try:
            boot = read_for(4)
            self.assertIsNone(process.poll(), boot)
            self.assertEqual(boot.strip(), "uart:~$")
            self.assertIn("zephyr-hello-shell", command("version"))
            status = command("log status")
            for module in ("app", "camera_stream", "audio_stream"):
                self.assertRegex(status, rf"{module}\s+\|\s+wrn\s+\|")
            command("log enable inf app")
            self.assertRegex(command("log status"), r"app\s+\|\s+inf\s+\|")
            command("log enable inf camera_stream")
            with socket.create_connection(("127.0.0.1", 8081), timeout=2) as client:
                client.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
                client.recv(128)
                self.assertIn("Camera stream client connected", read_for(0.5))
            read_for(0.5)
            command("log enable wrn")
            self.assertRegex(command("log status"), r"app\s+\|\s+wrn\s+\|")
            with socket.create_connection(("127.0.0.1", 8081), timeout=2) as client:
                client.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
                client.recv(128)
                self.assertNotIn("Camera stream client connected", read_for(0.5))
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            os.close(master)
            process.stderr.close()

    def test_errors_remain_visible(self):
        executable = os.environ["ZEPHYR_QUIET_BOOT_EXE"]
        with socket.socket() as occupied:
            occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            occupied.bind(("127.0.0.1", 8082))
            occupied.listen()
            result = subprocess.run(
                [executable, "-uart_stdinout", "-stop_at=2"],
                input="", capture_output=True, text=True, timeout=10
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("bind/listen failed:", result.stdout)
        self.assertIn("<err> audio_stream:", result.stdout)
        self.assertIn("uart:~$", result.stdout)
        self.assertNotIn("Microphone stream listening", result.stdout)


if __name__ == "__main__":
    unittest.main()
