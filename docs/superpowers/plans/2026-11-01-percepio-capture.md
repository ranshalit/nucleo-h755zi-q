# Percepio Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Capture Percepio RAM snapshots with the existing ST-LINK wrapper.

**Architecture:** Explicit `--trace-format` selects ELF preflight, dump commands,
output artifact and import instructions. Share connection, reset and recovery.

**Tech Stack:** Bash, Zephyr GDB, OpenOCD, Python unittest.

## Global Constraints

- CTF remains the default.
- No flashing, streaming, automatic format detection or firmware changes.
- Keep current snapshot, reset, delay and recovery semantics.

## Task 1: Add and verify Percepio snapshot support

**Files:** Modify `scripts/capture_zephyr_trace.sh`,
`scripts/test_capture_zephyr_trace.py` and `README.md`.

**Interfaces:** Existing `--elf`, `--reset`, `--seconds`, `--output` remain.
Add `--trace-format ctf|percepio`; Percepio outputs `trace.bin`.

- [x] Extend fake GDB to recognize `dump binary value`, simulate a null or
  uninitialized `RecorderDataPtr`, and reject mismatched ELF preflight.
  Add snapshot and recovery cases:

```python
def test_percepio_snapshot(self):
    result = self.run_capture("--trace-format", "percepio")
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue((self.output / "trace.bin").is_file())
    self.assertFalse((self.output / "metadata").exists())
    self.assertFalse((self.output / "channel0_0").exists())
    self.assertIn("dump binary value trace.bin *RecorderDataPtr", self.calls())
    self.assertNotIn("monitor reset", self.calls())
    self.assertIn("openocd stopped", self.calls())
```

- [x] Run `python3 scripts/test_capture_zephyr_trace.py -v`; new selector tests
  should fail, existing CTF cases should pass.
- [x] Add selector state and dispatch:

```bash
TRACE_FORMAT=ctf
# Parse --trace-format with the existing value-taking options.
case "$TRACE_FORMAT" in
  ctf)
    TRACE_FILE=channel0_0
    PREFLIGHT_ARGS=(-ex 'p sizeof(ram_tracing)' -ex "p &'tracing_backend_ram.c'::pos")
    ;;
  percepio)
    TRACE_FILE=trace.bin
    PREFLIGHT_ARGS=(-ex 'p sizeof(*RecorderDataPtr)' -ex 'p &RecorderDataPtr')
    ;;
  *) fail "unknown trace format: $TRACE_FORMAT (use ctf or percepio)" ;;
esac
```

  Gate metadata existence and copy on CTF. Pass `PREFLIGHT_ARGS` to existing
  offline GDB preflight. Record format in capture-info.
  Generate common GDB prefix through `monitor halt`; append existing CTF
  commands for CTF, or the following Percepio commands:

```gdb
set $trace_recorder = RecorderDataPtr
if $trace_recorder == 0
echo error: Percepio recorder is not initialized.\n
quit 1
end
if $trace_recorder->xEventBuffer.uxSize != sizeof($trace_recorder->xEventBuffer.uiBuffer)
echo error: invalid Percepio ring buffer size.\n
quit 1
end
dump binary value trace.bin *RecorderDataPtr
printf "Captured %u bytes\n", sizeof(*RecorderDataPtr)
```

  Before dumping also validate all initialized markers using little-endian
  32-bit reads at offsets 0, 4 and 8 of both marker arrays. Expected START
  values are `0x08070605`, `0x78777675`, `0xf8f7f6f5`; END values are
  `0x0d0c0b0a`, `0x74737271`, `0xf4f3f2f1`. Reject with `quit 1` on mismatch.
  Append shared `monitor resume` and `detach`. Check the selected file is
  nonempty and report View file instructions versus Trace Compass directory
  instructions.
- [x] Expand tests to cover reset/delay ordering, null pointer, corrupt size
  or markers, mismatched preflight before reset, invalid selectors, and dump
  failure recovery for Percepio. Retain existing CTF tests unchanged.
- [x] Document build versus capture commands in README:

```bash
scripts/run.sh hello board --trace-format percepio
scripts/capture_zephyr_trace.sh --trace-format percepio \
  --elf /tmp/stm32h7-zephyr-hello-shell/zephyr/zephyr.elf
```

  Explain `--reset --seconds 20`, circular overwrite behavior, matching flashed
  ELF and opening the emitted `trace.bin` in View.
- [x] Run `python3 scripts/test_capture_zephyr_trace.py -v`,
  `bash -n scripts/capture_zephyr_trace.sh` and `git diff --check`.
  If a Percepio ELF exists, use `scripts/gdb_zephyr_hello_shell.sh ELF -nx -batch
  -ex 'p sizeof(*RecorderDataPtr)' -ex 'p &RecorderDataPtr'` for real preflight.
  Do not flash merely to test capture; report hardware/View checks not performed.
- [x] Commit only the scoped files if the user chooses a commit at completion;
  include the required Copilot co-author trailer. Preserve unrelated files.
