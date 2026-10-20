# Zephyr LED-thread MPU protection design

## Goal

Protect the two NUCLEO-H755ZI-Q M7 LED blink threads from stack overflow and
unauthorized memory access, without moving the rest of the application into
userspace. Keep the existing shell, tracing, debugger, and native simulation
workflows available.

## Protection model

Enable `CONFIG_USERSPACE` for the M7 build and create only the yellow and red
LED blink threads as user threads. On the Cortex-M7 MPU, user mode restricts
each thread to its own stack and to explicitly granted memory and kernel
objects. The two threads will use separate memory domains, each granting only
that thread's LED state. Grant the shared board-I/O mutex and the relevant GPIO
controller device to both threads; do not grant the button or unrelated app
data. Start each thread only after its memory-domain and object permissions
have been installed.

Enable `CONFIG_HW_STACK_PROTECTION` in the M7 board configuration as additional
protection for supervisor-mode thread stacks. This option is not the LED
threads' isolation mechanism: Zephyr user-mode MPU restrictions bound their
stacks and protect them from one another. The supervisor-stack option applies
to supervisor-mode threads and consumes MPU resources.

The current LED descriptors are elements of one shared array. Restructure only
the state needed by the two blink threads into appropriately aligned,
individually grantable regions so that a thread does not receive access to the
other LED's mutable state. Supervisor code retains access to all LED state for
the existing API and shell behavior. Preserve error reporting from blink
threads through a path usable from user mode; do not silently discard GPIO
errors.

## Target scope and compatibility

Apply the protection Kconfig settings only to
`nucleo_h755zi_q/stm32h755xx/m7`. Keep the shell and all non-LED threads in
supervisor mode. Do not enable these settings for `native_sim` or MPS2: those
targets use the software LED implementation and do not emulate the STM32 MPU.

Keep the existing CTF tracing selections and trace backend unchanged. Userspace
adds system-call and context-switch work but does not disable kernel tracing.
GDB/SWD debugging remains available; a protection violation is expected to
appear as an MPU fault and may change the fault-handling path. These behaviors
must be checked rather than assumed. Native simulation should continue to test
application behavior, but cannot validate hardware MPU enforcement.

The finite MPU-region budget is a design constraint. Confirm at build time that
userspace domains, stack protection, and the existing non-cacheable SRAM3
mapping fit together. If they do not, do not silently weaken the per-thread
memory boundary; report the constraint and revisit the design.

## Errors and failure behavior

Install permissions before either user thread can run, preventing a startup
race where a thread accesses an object before it is granted. If permission or
domain setup fails, fail thread startup explicitly and leave the application
with a reported initialization error. Preserve blink-thread GPIO error
diagnostics using a userspace-safe mechanism. Invalid accesses should trigger
the normal Zephyr MPU/user-fault behavior and must not be converted into
success-shaped LED state.

## Build and validation

Build the M7 target with userspace and hardware stack protection enabled.
Build `native_sim/native/64` in its existing configuration to check that
simulated LED behavior and shared application services remain unchanged.

On the board, verify:

- Yellow and red LEDs continue blinking and respond correctly to suspend and
  resume behavior.
- Shell LED commands and application services continue working.
- CTF tracing remains enabled and produces usable trace data.
- GDB/SWD can inspect normal execution and an intentionally induced,
  test-only MPU violation.
- A test-only cross-access probe confirms one LED user thread cannot read or
  write the other thread's private LED state or stack.

Do not run fault-injection probes in the normal firmware. A successful build
alone does not prove MPU enforcement or runtime compatibility; board-level
checks are required for those acceptance criteria.

## References

- Zephyr 4.2 userspace overview:
  https://docs.zephyrproject.org/4.2.0/kernel/usermode/overview.html
- Zephyr 4.2 memory protection design:
  https://docs.zephyrproject.org/4.2.0/kernel/usermode/memory_domain.html
