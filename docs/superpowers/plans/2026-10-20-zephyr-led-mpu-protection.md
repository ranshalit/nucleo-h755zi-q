# Zephyr LED-thread MPU Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Isolate the two NUCLEO-H755ZI-Q M7 LED blink threads with Zephyr userspace MPU permissions and enable supervisor-thread stack protection.

**Architecture:** Enable `CONFIG_USERSPACE` and `CONFIG_HW_STACK_PROTECTION` only for the M7 target. Put each LED thread's mutable descriptor in its own MPU-aligned partition and memory domain, then create each thread unprivileged and grant only its own state, required GPIO/mutex/message-queue objects, and stack. Keep other application threads privileged and leave native_sim/MPS2 on their existing simulated I/O paths.

**Tech Stack:** Zephyr 4.2 kernel userspace, ARM Cortex-M7 MPU, Kconfig, C, existing native_sim runner, STM32CubeProgrammer/GDB on hardware.

## Global Constraints

- Apply the protection Kconfig settings only to `nucleo_h755zi_q/stm32h755xx/m7`.
- Keep the shell and all non-LED threads in supervisor mode.
- Do not enable these settings for `native_sim` or MPS2: those targets use the software LED implementation and do not emulate the STM32 MPU.
- Keep the existing CTF tracing selections and trace backend unchanged.
- Do not run fault-injection probes in the normal firmware.
- If the finite MPU-region budget is insufficient, report the constraint and revisit the design; do not weaken the per-thread memory boundary.

---

### Task 1: Enable M7 protection options

**Files:**
- Modify: `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`

**Interfaces:**
- Produces: M7 builds set `CONFIG_USERSPACE=y` and `CONFIG_HW_STACK_PROTECTION=y`.
- Leaves unchanged: `prj.conf`, native_sim configs, MPS2 config, and existing CTF settings.

- [ ] **Step 1: Enable the two M7-only Kconfig options**

Add to the M7 board config:

```conf
# User-mode MPU permissions isolate the LED blink threads.
CONFIG_USERSPACE=y
# Also protect stacks of supervisor-mode threads.
CONFIG_HW_STACK_PROTECTION=y
```

- [ ] **Step 2: Build the M7 app without flashing**

Run:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only \
  --build-dir build/zephyr-hello-shell-mpu
```

Expected: build succeeds and `build/zephyr-hello-shell-mpu/zephyr/.config` contains `CONFIG_USERSPACE=y`, `CONFIG_HW_STACK_PROTECTION=y`, `CONFIG_TRACING=y`, and `CONFIG_TRACING_CTF=y`.

- [ ] **Step 3: Build the existing simulator profile**

Run:

```bash
scripts/run_zephyr_hello_shell_native.sh \
  --profile localhost --build-only \
  --build-dir build/zephyr-hello-shell-native-mpu
```

Expected: native_sim builds with the existing simulated I/O implementation; no MPU setting is added to its profile.

- [ ] **Step 4: Commit the target-scoped configuration**

```bash
git add apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf
git commit -m "feat(zephyr): enable M7 MPU protections" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 2: Give each LED thread a private data partition

**Files:**
- Modify: `apps/zephyr-hello-shell/src/board_io.c`

**Interfaces:**
- Consumes: Existing `struct board_led`, `board_leds`, and `board_led_from_id()` behavior.
- Produces: Separate yellow/red LED descriptors addressable by the supervisor and grantable independently to user threads.

- [ ] **Step 1: Define isolated, aligned LED state storage**

Include `<zephyr/app_memory/mem_domain.h>`. Replace the yellow/red entries in the shared `struct board_led` array with separate MPU-aligned regions. Keep the green LED supervisor-only. Use an M7-compatible power-of-two partition size of at least 32 bytes and compile-time checks:

```c
#define LED_USER_REGION_SIZE 64U

struct board_led_region {
	struct board_led led;
	uint8_t padding[LED_USER_REGION_SIZE - sizeof(struct board_led)];
} __aligned(LED_USER_REGION_SIZE);

BUILD_ASSERT(sizeof(struct board_led) <= LED_USER_REGION_SIZE);
BUILD_ASSERT(sizeof(struct board_led_region) == LED_USER_REGION_SIZE);

static struct board_led green_led;
static struct board_led_region yellow_led_region;
static struct board_led_region red_led_region;

K_MEM_PARTITION_DEFINE(yellow_led_partition, &yellow_led_region,
		       sizeof(yellow_led_region), K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(red_led_partition, &red_led_region,
		       sizeof(red_led_region), K_MEM_PARTITION_P_RW_U_RW);
```

Add an `enum board_led_id` field to `struct board_led` and initialize the three descriptors with their current values:

```c
static struct board_led green_led = {
	.id = BOARD_LED_GREEN,
	.info = {
		.color = "green",
		.label = "LD1",
		.blinking = false,
		.blink_period_ms = 0,
	},
	.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios),
};

static struct board_led_region yellow_led_region = {
	.led = {
		.id = BOARD_LED_YELLOW,
		.info = {
			.color = "yellow",
			.label = "LD2",
			.blinking = true,
			.blink_period_ms = 400U,
		},
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(yellow_led), gpios),
	},
};

static struct board_led_region red_led_region = {
	.led = {
		.id = BOARD_LED_RED,
		.info = {
			.color = "red",
			.label = "LD3",
			.blinking = true,
			.blink_period_ms = 700U,
		},
		.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios),
	},
};
```

Replace the value array with a supervisor-only pointer table:

```c
static struct board_led *const board_leds[] = {
	&green_led,
	&yellow_led_region.led,
	&red_led_region.led,
};
```

Keep `board_led_from_id()`, count, query, and command behavior unchanged through that table.

- [ ] **Step 2: Build both targets after the data-layout change**

Run:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only \
  --build-dir build/zephyr-hello-shell-mpu
scripts/run_zephyr_hello_shell_native.sh \
  --profile localhost --build-only \
  --build-dir build/zephyr-hello-shell-native-mpu
```

Expected: both builds pass; current LED query and command APIs still compile for their target-selected implementations.

- [ ] **Step 3: Commit the private state layout**

```bash
git add apps/zephyr-hello-shell/src/board_io.c
git commit -m "refactor(zephyr): isolate LED state regions" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 3: Start only the two LED threads in user mode

**Files:**
- Modify: `apps/zephyr-hello-shell/src/board_io.c`

**Interfaces:**
- Consumes: `yellow_led_partition`, `red_led_partition`, and the two existing thread stacks.
- Produces: Yellow and red threads each assigned a distinct domain and explicit kernel-object permissions; all other threads remain privileged.

- [ ] **Step 1: Define one memory domain per LED thread**

Add statically allocated domains and partition pointer arrays:

```c
static struct k_mem_domain yellow_led_domain;
static struct k_mem_domain red_led_domain;

static struct k_mem_partition *yellow_led_partitions[] = {
	&yellow_led_partition,
};
static struct k_mem_partition *red_led_partitions[] = {
	&red_led_partition,
};
```

- [ ] **Step 2: Initialize domains and create both threads suspended**

In `board_blink_threads_start()`, initialize each domain once with `k_mem_domain_init()`. Create each thread with `K_USER` and `K_FOREVER`, then call `k_mem_domain_add_thread()` for its matching domain. Grant the owner thread access to `board_io_lock`, its `gpio.port` device, and the static GPIO-error message queue with `k_object_access_grant()`. Do not grant the other LED partition or the user-button device.

Start the supervisor-mode logger thread before starting either LED thread so that a GPIO error cannot block forever on an unserviced queue.

Use the existing `k_thread_create()` entry and preserve its current priority and period arguments. Create both threads suspended, add them to their corresponding domains, and grant their objects before starting either:

```c
yellow_tid = k_thread_create(
	&yellow_led_blink_thread, yellow_led_blink_stack,
	K_THREAD_STACK_SIZEOF(yellow_led_blink_stack), led_blink_thread,
	&yellow_led_region.led,
	INT_TO_POINTER(yellow_led_region.led.info.blink_period_ms), NULL,
	LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);
red_tid = k_thread_create(
	&red_led_blink_thread, red_led_blink_stack,
	K_THREAD_STACK_SIZEOF(red_led_blink_stack), led_blink_thread,
	&red_led_region.led,
	INT_TO_POINTER(red_led_region.led.info.blink_period_ms), NULL,
	LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);

ret = k_mem_domain_add_thread(&yellow_led_domain, yellow_tid);
if (ret == 0) {
	ret = k_mem_domain_add_thread(&red_led_domain, red_tid);
}
if (ret != 0) {
	k_thread_abort(yellow_tid);
	k_thread_abort(red_tid);
	return ret;
}
k_object_access_grant(&board_io_lock, yellow_tid);
k_object_access_grant(yellow_led_region.led.gpio.port, yellow_tid);
k_object_access_grant(&board_io_error_queue, yellow_tid);
k_object_access_grant(&board_io_lock, red_tid);
k_object_access_grant(red_led_region.led.gpio.port, red_tid);
k_object_access_grant(&board_io_error_queue, red_tid);

k_thread_name_set(yellow_tid, "yellow_led_blink");
k_thread_name_set(red_tid, "red_led_blink");
k_thread_start(yellow_tid);
k_thread_start(red_tid);
```

On any domain setup failure, abort both still-suspended blink threads, latch and return the error, and do not set `blink_threads_started`. If `board_blink_threads_start()` is called again after partial setup, return the latched error instead of reinitializing an already initialized memory domain.

- [ ] **Step 3: Preserve GPIO error diagnostics in supervisor mode**

Add a static kernel message queue for small `{ enum board_led_id, int error; }` records and a dedicated supervisor-mode logger thread. For example:

```c
struct board_led_error {
	enum board_led_id led_id;
	int error;
};

K_MSGQ_DEFINE(board_led_error_queue, sizeof(struct board_led_error), 4, 4);
K_THREAD_STACK_DEFINE(board_io_error_stack, 1024);
static struct k_thread board_io_error_thread;

static void board_io_error_thread_entry(void *arg1, void *arg2, void *arg3)
{
	struct board_led_error error;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		ret = k_msgq_get(&board_led_error_queue, &error, K_FOREVER);
		if (ret != 0) {
			printk("LED error queue receive failed: %d\n", ret);
			continue;
		}
		if (error.led_id < BOARD_LED_GREEN ||
		    error.led_id >= BOARD_LED_COUNT) {
			printk("LED error queue received invalid LED ID: %d\n",
			       error.led_id);
			continue;
		}
		printk("%s LED toggle failed: %d\n",
		       board_leds[error.led_id]->info.color, error.error);
	}
}
```

Create and name this logger thread with `k_thread_create()` using `board_io_error_stack`, `K_THREAD_STACK_SIZEOF(board_io_error_stack)`, `board_io_error_thread_entry`, priority `LED_BLINK_THREAD_PRIORITY + 1`, options `0`, and delay `K_NO_WAIT`. Start it before starting either LED thread. Grant the queue object to both LED threads. On GPIO failure, each user thread submits a record with `k_msgq_put(&board_led_error_queue, &error, K_FOREVER)` and calls `k_oops()` if submission unexpectedly fails. The message queue copies records into kernel-owned storage. Do not call `printk()` directly from a user thread or expose the queue's storage as a memory-domain partition.

- [ ] **Step 4: Build M7 and native_sim**

Run:

```bash
scripts/flash_zephyr_hello_shell.sh --build-only \
  --build-dir build/zephyr-hello-shell-mpu
scripts/run_zephyr_hello_shell_native.sh \
  --profile localhost --build-only \
  --build-dir build/zephyr-hello-shell-native-mpu
```

Expected: the M7 build accepts the user-mode threads, object grants, and MPU partitions; native_sim continues using `board_io_native_sim.c` unchanged.

- [ ] **Step 5: Commit user-mode thread startup**

```bash
git add apps/zephyr-hello-shell/src/board_io.c
git commit -m "feat(zephyr): isolate LED blink threads" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 4: Add a test-only cross-access probe

**Files:**
- Modify: `apps/zephyr-hello-shell/Kconfig`
- Modify: `apps/zephyr-hello-shell/src/board_io.c`
- Create: `apps/zephyr-hello-shell/conf/nucleo_mpu_probe.conf`

**Interfaces:**
- Produces: A test-only shell command that deliberately attempts cross-partition access from a user thread.
- Default production behavior: test option is disabled and no probe command or fault injection is built.

- [ ] **Step 1: Add a disabled-by-default test option**

Add to `Kconfig`:

```kconfig
config APP_MPU_PROTECTION_TEST
	bool "Enable the test-only LED MPU cross-access probe"
	depends on USERSPACE
	default n
```

Create `conf/nucleo_mpu_probe.conf`:

```conf
CONFIG_APP_MPU_PROTECTION_TEST=y
```

- [ ] **Step 2: Implement the test-only probe command**

Under `#if defined(CONFIG_APP_MPU_PROTECTION_TEST)`, register `mpu_probe` with `SHELL_CMD_REGISTER`. It creates a temporary user thread with `K_USER` and `K_FOREVER`, adds it to `yellow_led_domain`, prints the probe mode, and starts it. Give the probe a static `K_THREAD_STACK_DEFINE` stack. Support the modes `state-read`, `state-write`, `stack-read`, and `stack-write`; pass the red LED state address or red LED stack address as the probe argument. The entry performs the selected volatile access:

```c
static void mpu_probe_thread(void *arg1, void *arg2, void *arg3)
{
	volatile uint32_t *address = arg1;
	int write_access = POINTER_TO_INT(arg2);

	if (write_access != 0) {
		*address = 0xA5A5A5A5U;
	} else {
		volatile uint32_t value = *address;
		ARG_UNUSED(value);
	}
	ARG_UNUSED(arg3);
}
```

Implement the shell handler with this flow:

```c
static int cmd_mpu_probe(const struct shell *shell, size_t argc, char **argv)
{
	volatile uint32_t *address;
	bool write_access;
	k_tid_t tid;
	int ret;

	if (argc != 2) {
		shell_error(shell,
			    "usage: mpu_probe state-read|state-write|stack-read|stack-write");
		return -EINVAL;
	}
	if (strcmp(argv[1], "state-read") == 0 ||
	    strcmp(argv[1], "state-write") == 0) {
		address = &red_led_region.led.info.blink_period_ms;
	} else if (strcmp(argv[1], "stack-read") == 0 ||
		   strcmp(argv[1], "stack-write") == 0) {
		address = (volatile uint32_t *)red_led_blink_stack;
	} else {
		shell_error(shell, "unknown probe mode");
		return -EINVAL;
	}
	write_access = (strcmp(argv[1], "state-write") == 0 ||
			strcmp(argv[1], "stack-write") == 0);
	tid = k_thread_create(&mpu_probe_thread_data, mpu_probe_stack,
			      K_THREAD_STACK_SIZEOF(mpu_probe_stack),
			      mpu_probe_thread, (void *)address,
			      INT_TO_POINTER(write_access), NULL,
			      LED_BLINK_THREAD_PRIORITY, K_USER, K_FOREVER);
	ret = k_mem_domain_add_thread(&yellow_led_domain, tid);
	if (ret != 0) {
		k_thread_abort(tid);
		shell_error(shell, "could not assign probe memory domain: %d", ret);
		return ret;
	}
	k_thread_name_set(tid, "mpu_probe");
	shell_print(shell, "started %s; expected MPU fault", argv[1]);
	k_thread_start(tid);
	return 0;
}

SHELL_CMD_REGISTER(mpu_probe, NULL, "Test LED MPU isolation", cmd_mpu_probe);
```

Define `mpu_probe_thread_data` and `mpu_probe_stack` at file scope with `struct k_thread` and `K_THREAD_STACK_DEFINE`. The expected result is an M7 MPU fault before the probe can return successfully. Keep the command and probe code out of normal builds.

- [ ] **Step 3: Build the probe image separately**

From the repository root, run with the repository's Zephyr SDK environment configured:

```bash
REPO_ROOT="$PWD"
(cd "$REPO_ROOT/vendor/zephyrproject/zephyr" &&
  west build -p always \
    -b nucleo_h755zi_q/stm32h755xx/m7 \
    "$REPO_ROOT/apps/zephyr-hello-shell" \
    -d "$REPO_ROOT/build/zephyr-hello-shell-mpu-probe" -- \
    "-DEXTRA_CONF_FILE=$REPO_ROOT/apps/zephyr-hello-shell/conf/nucleo_mpu_probe.conf")
```

Expected: probe image builds with `CONFIG_APP_MPU_PROTECTION_TEST=y`. The normal M7 config remains at the default `n`.

- [ ] **Step 4: Commit the test-only probe**

```bash
git add apps/zephyr-hello-shell/Kconfig \
  apps/zephyr-hello-shell/src/board_io.c \
  apps/zephyr-hello-shell/conf/nucleo_mpu_probe.conf
git commit -m "test(zephyr): probe LED MPU isolation" \
  -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 5: Validate runtime compatibility

**Files:**
- Verify: `apps/zephyr-hello-shell/boards/nucleo_h755zi_q_stm32h755xx_m7.conf`
- Verify: `apps/zephyr-hello-shell/src/board_io.c`
- Verify: `apps/zephyr-hello-shell/src/board_io_native_sim.c`

**Interfaces:**
- Consumes: Production M7 firmware from Tasks 1–3 and the test-only probe image from Task 4.
- Produces: Recorded build, simulator, normal-board, tracing/debugger, and isolation results.

- [ ] **Step 1: Verify production M7 Kconfig**

Build with `scripts/flash_zephyr_hello_shell.sh --build-only --build-dir build/zephyr-hello-shell-mpu` and check:

```bash
grep -E '^CONFIG_(USERSPACE|HW_STACK_PROTECTION|TRACING|TRACING_CTF|TRACING_BACKEND_RAM)=' \
  build/zephyr-hello-shell-mpu/zephyr/.config
```

Expected: protection and existing tracing options remain enabled.

- [ ] **Step 2: Verify native_sim regression**

Build and run the `localhost` profile with:

```bash
scripts/run_zephyr_hello_shell_native.sh \
  --profile localhost --build-only \
  --build-dir build/zephyr-hello-shell-native-mpu
scripts/run_zephyr_hello_shell_native.sh \
  --profile localhost --run-only \
  --build-dir build/zephyr-hello-shell-native-mpu
```

Check shell LED status/on/off, blinking, and the existing HTTP dashboard/API. The simulator result is application regression coverage, not evidence of MPU enforcement.

- [ ] **Step 3: Verify normal firmware on the Nucleo**

Use the existing `scripts/flash_zephyr_hello_shell.sh` flash/validation flow. Check yellow/red blink periods, shell LED actions, suspend/resume behavior, dashboard/API startup, and absence of MPU faults during normal execution.

- [ ] **Step 4: Verify tracing and debugging**

Confirm the CTF and RAM backend settings in `.config`, capture trace data using the existing local tracing setup, and attach through the existing GDB/SWD workflow during normal operation. The repository has no checked-in trace-capture command; if the local capture setup is unavailable, record runtime trace capture as unverified rather than claiming success. Confirm the probe image's intentional user-mode fault is visible to the debugger as an MPU fault. Report hardware checks that cannot be run because a board, debugger, or trace-capture setup is unavailable.

- [ ] **Step 5: Run the test-only cross-access probe**

Flash only the separate probe image from the Zephyr workspace:

```bash
REPO_ROOT="$PWD"
(cd "$REPO_ROOT/vendor/zephyrproject/zephyr" &&
  west flash -d "$REPO_ROOT/build/zephyr-hello-shell-mpu-probe" \
    --runner stm32cubeprogrammer)
```

Attach the debugger and run each of `mpu_probe state-read`, `mpu_probe state-write`, `mpu_probe stack-read`, and `mpu_probe stack-write` separately. Inspect each fault. Expected: every cross-access faults before it can read or modify the red LED's private state or stack. Do not flash this image as the normal app.
