# App runner dispatcher design

## Goal

One entry point to run each repo application on each target without
remembering per-app script names.

## Interface

```
scripts/run.sh <app> <target> [args...]
scripts/run.sh list | -h | --help
```

- `app`: `hello` (dashboard/webserver), `iperf` (alias `zperf`),
  `cyclictest` (alias `zyclictest`).
- `target`: `board`, `qemu`, `native_sim` (alias `native`).
- Remaining `args` are passed unchanged to the underlying script, e.g.
  `scripts/run.sh hello qemu --network tap`, `scripts/run.sh iperf board --restore`,
  `scripts/run.sh hello board --help`.

## Dispatch table

| app \ target | board                         | qemu                             | native_sim                         |
|--------------|-------------------------------|----------------------------------|------------------------------------|
| hello        | `flash_zephyr_hello_shell.sh` | `run_zephyr_hello_shell_qemu.sh` | `run_zephyr_hello_shell_native.sh` |
| iperf        | `flash_zephyr_zperf.sh`       | unsupported                      | unsupported                        |
| cyclictest   | `flash_zephyr_zyclictest.sh`  | unsupported                      | unsupported                        |

## Behavior

- Thin dispatcher: a bash `case` table that `exec bash <script> "$@"`.
  No build logic of its own; existing scripts stay the source of truth.
- `list`/`-h`/`--help` print usage plus the matrix and exit 0.
- Missing app/target prints usage to stderr and exits 2.
- Unknown app or target exits 2 with an error naming the bad value.
- Unsupported combination exits 2 with
  `error: <app> is not supported on <target> (supported: ...)`.
- `RUN_SCRIPTS_DIR` overrides the directory holding the child scripts
  (used by tests).

## Testing

`scripts/test_run.sh` points `RUN_SCRIPTS_DIR` at stub scripts that record
their name and arguments, then checks every supported cell, aliases, argument
passthrough, help output, and each error path (exit code and message).

## Docs

Add a short "Running applications" section to `README.md`.

## Out of scope

Adding qemu/native_sim support for iperf or cyclictest; retiring existing
scripts.
