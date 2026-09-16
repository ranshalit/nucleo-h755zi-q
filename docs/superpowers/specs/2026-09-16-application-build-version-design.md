# Application Build Version Design

## Goal

Expose the `zephyr-hello-shell` application build time at startup and through a
top-level `version` shell command.

## Build Metadata

CMake computes one ISO 8601 UTC timestamp in the form
`YYYY-MM-DDTHH:MM:SSZ`. If `SOURCE_DATE_EPOCH` is set, CMake derives the
timestamp from that value so reproducible builds produce identical metadata.
Otherwise, CMake uses the current UTC time when configuring the build.

The timestamp is passed to the application as a compile definition. It is
immutable and does not depend on a runtime clock.

## User Interface

At startup, the application prints:

```text
Build time: <timestamp>
```

The top-level shell command:

```text
version
```

prints the application name and build timestamp. This application command is
distinct from Zephyr's existing `kernel version` command.

## Failure Handling

An invalid nonnumeric `SOURCE_DATE_EPOCH` causes CMake configuration to fail
with an explicit error. No runtime failure path is needed because the metadata
is compiled into the image.

## Verification

Run a pristine application build and confirm it completes. Inspect the output
image for the expected application name, `Build time:` label, and ISO 8601
timestamp. Runtime shell validation and flashing remain separate operations.
