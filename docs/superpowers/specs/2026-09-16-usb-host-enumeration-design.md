# STM32H7 USB Host Enumeration Design

## Goal

Make `apps/zephyr-hello-shell` act as a USB Full-Speed host on CN13 and
reliably enumerate the attached HP UVC camera. Report connection state and
the standard USB device descriptor on the existing Zephyr UART shell.

This milestone proves the physical USB path and host protocol work. It does
not capture or stream video.

## Scope and Constraints

- Target: NUCLEO-H755ZI-Q CM7 application, CN13 USB OTG_FS.
- The camera is a USB device. The STM32 is the host: it supplies VBUS,
  detects attach, resets the bus, assigns an address, and reads descriptors.
- Preserve the committed PD10/U18 VBUS enable: early boot asserts it and the
  Zephyr GPIO hog keeps it asserted. Do not move or duplicate that ownership.
- Require SB77 and SB76 to be ON and the board powered before connecting
  CN13, per UM2407.
- Keep Zephyr as the RTOS, shell, Ethernet, and HTTP application framework.
- Do not patch Zephyr upstream sources for this milestone.
- Do not enable Zephyr `USB_HOST_STACK`: the checked-in Zephyr revision has
  no STM32H7 UHC implementation.

## Architecture

Add an application-local `src/usb_host/` module with three boundaries:

1. `stm32_hcd_adapter.c` configures USB_OTG_FS in host mode through the
   vendored STM32H7 HAL HCD API. It owns clocks, PA11/PA12 alternate functions,
   OTG_FS interrupts, and HAL callbacks. It publishes attach, detach,
   overcurrent, reset, and completed transfer events to the host state machine.
2. `usb_enumerator.c` is controller-independent protocol logic. It performs
   the minimum USB Chapter 9 sequence: wait for attach, debounce, reset port,
   read the first eight bytes of the device descriptor, set address, then read
   the complete 18-byte descriptor. It validates descriptor length/type and
   captures vendor ID, product ID, device class, and USB version.
3. `usb_host_shell.c` exposes `usbhost status` and `usbhost descriptor`.
   It reports explicit states and errors; it never claims a camera is usable
   merely because VBUS is enabled.

The existing `boot_optimizations.c` and GPIO hog remain the only VBUS control.
The adapter begins only after Zephyr kernel initialization, when the scheduler
and interrupt plumbing are available.

## State and Error Model

States are `idle`, `attached`, `resetting`, `addressing`, `described`,
`detached`, and `error`. Every transition records a reason.

Failures are retained in status and printed once to UART:

- no attach before the configured timeout;
- VBUS overcurrent (PG7/SB76);
- reset or speed-detection failure;
- control-transfer timeout, stall, or HAL error;
- malformed/short descriptor;
- detach during enumeration.

Enumeration retries only after a fresh attach. It must not continuously reset
or power-cycle a failing device.

## Configuration and Hardware

Add narrowly scoped application Kconfig settings for host-mode adapter memory,
interrupt priority, debounce/reset/control-transfer timeouts, and logging.
Extend the board overlay only when needed for host pinctrl, PG7 overcurrent
input, and OTG_FS interrupt support. It must retain current PA11/PA12 routing
and the PD10 GPIO hog.

At boot, log a concise `usbhost: ready` message. On camera insertion, expected
success output includes VID/PID and descriptor fields. This is enumeration
evidence, not a promise that the camera LED illuminates.

## Validation

Host tests cover descriptor validation and state-machine transitions with a
fake HCD event/transfer interface: successful enumeration, short descriptor,
bad descriptor type, timeout, overcurrent, and detach during enumeration.

Build validation uses the existing Zephyr SDK and
`scripts/flash_zephyr_hello_shell.sh --build-only`. Hardware validation:

1. Confirm SB76/SB77 are ON and LD8/VBUS is present at CN13.
2. Flash the app and open `/dev/ttyACM0`.
3. Insert the HP camera and run `usbhost status` plus `usbhost descriptor`.
4. Confirm an attached/described state and the descriptor's VID/PID.
5. Confirm LEDs, Ethernet, shell, and HTTP remain operational.

## Deferred Follow-up

UVC streaming is a separate design. It requires configuration-descriptor
parsing, interface/alternate-setting selection, isochronous endpoint
scheduling, frame-buffer ownership, and a consumer such as Ethernet HTTP.
ST's documented stock host classes do not establish a ready-made UVC path, so
that phase must select or implement a UVC host class explicitly.
