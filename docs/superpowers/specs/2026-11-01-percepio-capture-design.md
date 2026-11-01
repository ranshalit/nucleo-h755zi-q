# Percepio Snapshot Capture

## Scope

Extend `scripts/capture_zephyr_trace.sh` with `--trace-format ctf|percepio`.
CTF remains the default. Keep the current snapshot, explicit reset, delay,
matching ELF, new output directory, debug connection and recovery semantics.
No flashing, streaming, automatic format detection or firmware changes.

## Capture

CTF continues dumping `ram_tracing` up to its recorded position to `channel0_0`
with matching CTF metadata. Percepio dumps the full `*RecorderDataPtr` object to
`trace.bin`, including its header, object table, timestamps and ring buffer.
The pinned Percepio RingBuffer stream port exports this compatibility symbol.
Only CTF requires or copies CTF metadata.

Preflight checks the selected format's ELF symbols without connecting to the
board. After halting, Percepio checks that its recorder pointer is non-null and
its initialized buffer markers and event-buffer size match the expected layout
before dumping. Invalid state fails without reporting successful capture.
Existing error recovery resumes/detaches and stops only the owned debug server.

Log the selected format with existing capture information. Report the resulting
file and Percepio View opening instructions for Percepio, retaining the existing
Trace Compass directory instructions for CTF.

Percepio's configured circular buffer overwrites old events; a reset begins a
new recording but a delayed dump can contain only the most recent events.
CTF retains its stop-when-full behavior. Document this difference.

## Validation

Extend existing fake-GDB/OpenOCD tests to cover Percepio snapshots, reset order,
null or uninitialized recorder rejection, failed dump recovery, format-symbol
mismatch before reset, invalid format arguments and absence of CTF artifacts.
Run the existing capture test suite, Bash syntax and whitespace checks.
Where available, use the built Percepio ELF with real GDB for offline symbol
preflight and debugger-script parsing. Hardware capture and View import are
reported separately if they cannot be verified without changing board state.
