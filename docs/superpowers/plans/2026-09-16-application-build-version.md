# Application Build Version Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reproducible UTC build timestamp to application startup output and a top-level `version` shell command.

**Architecture:** CMake computes one ISO 8601 UTC timestamp during configuration and exports it as a quoted compile definition. The existing application entry point consumes that immutable string for startup and shell output; no runtime clock is required.

**Tech Stack:** CMake 3.20+, Zephyr 4.2, C, Zephyr shell

## Global Constraints

- Timestamp format is exactly `YYYY-MM-DDTHH:MM:SSZ`.
- Use `SOURCE_DATE_EPOCH` when set; otherwise use current UTC configuration time.
- Reject a nonnumeric `SOURCE_DATE_EPOCH` during CMake configuration.
- Keep Zephyr's existing `kernel version` command unchanged.
- Do not add a runtime clock dependency.

---

### Task 1: Generate and expose application build metadata

**Files:**
- Modify: `apps/zephyr-hello-shell/CMakeLists.txt`
- Modify: `apps/zephyr-hello-shell/src/main.c`

**Interfaces:**
- Consumes: CMake environment variable `$ENV{SOURCE_DATE_EPOCH}` when present.
- Produces: C string macro `APP_BUILD_TIMESTAMP`; top-level shell command `version`.

- [ ] **Step 1: Verify the command and timestamp are absent**

Run:

```bash
rg -n 'APP_BUILD_TIMESTAMP|SHELL_CMD_REGISTER\(version|Build time:' \
  apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/main.c
```

Expected: no matches and exit status 1.

- [ ] **Step 2: Add reproducible timestamp generation**

In `apps/zephyr-hello-shell/CMakeLists.txt`, immediately after
`project(zephyr_hello_shell)`, add:

```cmake
if(DEFINED ENV{SOURCE_DATE_EPOCH} AND
   NOT "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "SOURCE_DATE_EPOCH must be a nonnegative integer")
endif()

string(TIMESTAMP APP_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(app PRIVATE
  APP_BUILD_TIMESTAMP=\"${APP_BUILD_TIMESTAMP}\"
)
```

CMake's `string(TIMESTAMP)` uses `SOURCE_DATE_EPOCH` when that environment
variable exists, and otherwise uses the current time.

- [ ] **Step 3: Verify invalid epoch rejection**

Run:

```bash
SOURCE_DATE_EPOCH=invalid \
  ./scripts/flash_zephyr_hello_shell.sh --build-only \
  --build-dir /tmp/stm32h7-version-invalid
```

Expected: configuration fails with
`SOURCE_DATE_EPOCH must be a nonnegative integer`.

- [ ] **Step 4: Add startup and shell output**

In `apps/zephyr-hello-shell/src/main.c`, before the LED subcommand
declaration, add:

```c
static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "zephyr-hello-shell");
	shell_print(shell, "Build time: %s", APP_BUILD_TIMESTAMP);
	return 0;
}

SHELL_CMD_REGISTER(version, NULL, "Show application build information", cmd_version);
```

In `main()`, directly after the existing `Hello World!` line, add:

```c
	printf("Build time: %s\n", APP_BUILD_TIMESTAMP);
```

- [ ] **Step 5: Build twice with a fixed epoch**

Run:

```bash
SOURCE_DATE_EPOCH=0 \
  ./scripts/flash_zephyr_hello_shell.sh --build-only \
  --build-dir /tmp/stm32h7-version-build
```

Expected: exit status 0 and `zephyr.elf` is produced.

Run:

```bash
strings /tmp/stm32h7-version-build/zephyr/zephyr.elf |
  grep -Fx '1970-01-01T00:00:00Z'
```

Expected:

```text
1970-01-01T00:00:00Z
```

Run the same build command again, then repeat the `strings` check.

Expected: the same fixed timestamp is present after both pristine builds.

- [ ] **Step 6: Verify source and diff quality**

Run:

```bash
rg -n 'APP_BUILD_TIMESTAMP|SHELL_CMD_REGISTER\(version|Build time:' \
  apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/main.c &&
git --no-pager diff --check -- \
  apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/main.c
```

Expected: CMake generation, startup output, and shell registration all match;
`git diff --check` reports no errors.

- [ ] **Step 7: Commit**

```bash
git add \
  apps/zephyr-hello-shell/CMakeLists.txt \
  apps/zephyr-hello-shell/src/main.c
git commit -m "feat: expose application build time" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
