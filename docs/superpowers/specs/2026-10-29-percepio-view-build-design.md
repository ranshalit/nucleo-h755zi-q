# Percepio View Build Design

## Goal

Keep the existing CTF/Trace Compass firmware as the default while adding a
Percepio TraceRecorder build mode for Percepio View. The first implementation
step adds the configuration and proves that the M7 firmware builds; capture
and import into View are out of scope.

## Design

Zephyr's tracing-format Kconfig is a choice, so a single firmware image cannot
emit both CTF and Percepio TraceRecorder events. Support both workflows through
separate builds instead:

- The default remains the existing CTF configuration.
- `scripts/run.sh hello board --trace-format percepio --build-only` selects a
  Percepio configuration overlay.
- `--trace-format ctf` explicitly selects the existing CTF mode.

The Percepio overlay disables CTF and enables `CONFIG_PERCEPIO_TRACERECORDER`,
startup recording, and the RAM ring-buffer stream port. Preserve the existing
trace dump workflow until a separate capture/import implementation is designed.

The Percepio module is already available in the pinned Zephyr workspace at
`vendor/zephyrproject/modules/debug/percepio`.

## Validation

Build the M7 application using the Percepio selector and build-only mode. Check
the resolved `.config` confirms Percepio TraceRecorder and its ring buffer are
enabled and CTF is disabled. Also verify the default CTF build remains unchanged.
