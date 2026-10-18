I’ve now read `usb_debug.md`. The investigation has already narrowed the problem considerably:

- VBUS and the active-low PD10/JP8 path work.
- `PA9` VBUS sensing works.
- USB33, HSI48, OTG host mode, port reset, and port enable work.
- The ID pin is irrelevant to the remaining failure; grounding it did not help.
- The physical USB path is good: stock Zephyr CDC device mode enumerates and transfers data through the same connector and pins.
- ST’s USB Host Core fails identically to the custom enumerator.
- Three different USB devices fail on the first EP0 SETUP transaction.
- The failure is therefore almost certainly in the host transaction path, especially the STM32 HCD channel/register setup or HAL/LL interaction—not VBUS, enumeration state sequencing, or the board wiring.

The most important current log is:

```text
ST USBH: USB Device Connected
ST USBH: USB Device Reset Completed
ST USBH: device connected
ST USBH: pipe 0 URB=4 HC_state=2
HCINT=0x00000000 HCINTMSK=0x000004ff
HCTSIZ=0x60080008 HCCHAR=0x20100040
HPRT=0x00021405 PA9=1 PG7_nFAULT=1
ST USBH: unrecovered error
ST USBH ERROR: Control error: Device not responding
```

## Main issue I see

The document says the first transfer is reported as `URB=4`, described as `URB_STALL`, while simultaneously:

```text
HCINT=0x00000000
```

That combination is suspicious. A genuine USB STALL should normally be reflected by the host-channel interrupt status, typically `STALL` in `HCINT`, unless the HAL has already cleared or consumed the interrupt before the diagnostic reads it.

In other words, the current result may not mean “the device deliberately returned STALL.” It may mean:

1. the channel was enabled;
2. no valid transfer-completion/error interrupt was observed;
3. the HAL/adapter later classified the channel as `URB_STALL` or another terminal state;
4. the software reported that classification after the hardware state had already been cleared.

I would not yet trust `URB=4` as proof that the camera/device sent a STALL.

## Highest-value next test

Instrument the transaction at three exact points:

### 1. Immediately before enabling the channel

Dump:

```c
HCCHAR
HCSPLT
HCTSIZ
HCINT
HCINTMSK
HAINT
HAINTMSK
GINTSTS
GINTMSK
HPRT
```

Also dump the actual setup buffer bytes:

```text
80 06 00 01 00 00 12 00
```

For the initial descriptor request, the setup packet must be:

```c
bmRequestType = 0x80;  // device-to-host, standard, device
bRequest      = 0x06; // GET_DESCRIPTOR
wValue        = 0x0100;
wIndex        = 0x0000;
wLength       = 0x0012; // or 0x0008 for the first request
```

Be careful about the STM32 HCD’s setup-buffer representation. The setup packet must be copied into the format expected by `HAL_HCD_HC_SubmitRequest()`. Do not assume that a normal C struct layout is sufficient—inspect the generated 32-bit words and byte ordering.

### 2. Immediately after enabling the channel

Read the same registers without calling any HAL function that may clear status:

```c
printf("after enable: HCCHAR=%08lx HCTSIZ=%08lx "
       "HCINT=%08lx HAINT=%08lx GINTSTS=%08lx\r\n",
       ...);
```

Then poll for a short bounded interval, for example:

```c
for (unsigned i = 0; i < 100000; ++i) {
    uint32_t hcint = USBx_HC[ch].HCINT;
    uint32_t haint = USBx->HAINT;
    uint32_t gint = USBx->GINTSTS;

    if (hcint || haint || (gint & USB_OTG_GINTSTS_HCINT)) {
        printf("event i=%u HCINT=%08lx HAINT=%08lx GINTSTS=%08lx\r\n",
               i, hcint, haint, gint);
        break;
    }
}
```

### 3. Inside the IRQ/HAL callback

Record the state before and after `HAL_HCD_IRQHandler()`:

```c
printf("IRQ before: GINTSTS=%08lx HAINT=%08lx HCINT=%08lx\r\n", ...);

HAL_HCD_IRQHandler(&hhcd);

printf("IRQ after: GINTSTS=%08lx HAINT=%08lx HCINT=%08lx "
       "urb=%d hcstate=%d\r\n", ...);
```

This will tell you whether:

- the interrupt never occurs;
- the interrupt occurs but is masked;
- the HAL clears the status;
- the HAL changes the URB state without a corresponding `HCINT` bit;
- the channel is disabled or halted before the diagnostic runs.

## Check the SETUP packet path first

The register value:

```text
HCTSIZ=0x60080008
```

looks broadly plausible for an 8-byte SETUP transaction:

- transfer size: 8;
- one packet;
- SETUP PID.

But the register alone does not prove that the FIFO contains the correct setup bytes.

Verify the complete sequence:

```c
HAL_HCD_HC_Init(&hhcd, ch,
                ep_num,
                dev_addr,
                speed,
                EP_TYPE_CTRL,
                max_packet_size);

HAL_HCD_HC_SubmitRequest(&hhcd,
                         ch,
                         ep_num,
                         direction,
                         EP_TYPE_CTRL,
                         setup_data,
                         transfer_length,
                         1);
```

For a control transfer, make sure:

- `dev_addr` is zero for the initial descriptor request.
- `ep_num` is zero.
- `speed` matches `HAL_HCD_GetCurrentSpeed()`.
- `max_packet_size` is 64 only if the device has not yet supplied a smaller EP0 size; for the first request, 8 is the safest test.
- `direction` is the direction expected by the HAL for the SETUP stage—not simply the direction of the eventual data stage.
- `do_ping` is disabled for full-speed control transfers.
- the setup data is aligned and remains valid until the HAL has written it to the FIFO.

I would explicitly test the first request with an EP0 MPS of 8:

```c
HAL_HCD_HC_Init(&hhcd,
                0,              // channel
                0,              // endpoint 0
                0,              // address 0
                USBH_SPEED_FULL,
                EP_TYPE_CTRL,
                8);
```

The document says an MPS-8 trial was made, but it is worth confirming that it changed the actual `HCCHAR` register and that the first request did not silently revert to 64. The logged:

```text
HCCHAR=0x20100040
```

contains an MPS-looking value of `0x40`, so the failing transfer shown in the log is definitely using 64-byte EP0 MPS. That is legal only if the device’s initial EP0 behavior accepts it; many USB devices require the initial descriptor request to use the default 8-byte maximum packet size.

## Important possible bug: initial EP0 MPS

For USB enumeration, the standard sequence is:

1. Reset the port.
2. Send `GET_DESCRIPTOR(Device)` with:
   - address 0;
   - endpoint 0;
   - maximum packet size 8;
   - requested length 8.
3. Parse byte 7, `bMaxPacketSize0`.
4. Reinitialize EP0 using that value.
5. Request the full 18-byte descriptor.

Your log shows:

```text
HCTSIZ=0x60080008
HCCHAR=0x20100040
```

The transfer length is 8, but the channel MPS appears to be 64. That mismatch is my strongest software-level suspicion from the document.

Try enforcing both:

```c
request_length = 8;
ep0_mps = 8;
```

for the first request, and only change to the device-reported MPS after receiving those eight bytes.

If the ST USB Host Core currently initializes EP0 to 64 before the first request, patch the low-level pipe setup so the initial control pipe uses 8. Do not change only the requested transfer length; change the host-channel maximum packet size too.

## Verify FIFO writes

Since Device mode works but Host mode fails, inspect the host FIFO path. The HCD must write the SETUP packet into the non-periodic TX FIFO after channel setup.

Before enabling the channel, log:

```c
GRXFSIZ
HNPTXFSIZ
HPTXFSIZ
DIEPTXF0_HNPTXFSIZ
GDFIFOCFG
```

Then verify that the FIFO write count matches:

```text
ceil(8 / 4) = 2 32-bit words
```

For a SETUP transfer, the FIFO should receive exactly two words representing the eight setup bytes. A useful diagnostic is to temporarily log the words immediately before writing them:

```text
setup words: 0x00000680 0x00120000
```

The exact values depend on the HAL’s expected packing, so derive them from the actual setup buffer and compare them with ST’s `USB_HC_StartXfer()` implementation.

Also check whether the host TX FIFO is empty or full when the channel is enabled. If the channel is enabled before the setup words are available, the core may issue a malformed transaction.

## Inspect the HCD interrupt masks

The log shows:

```text
HCINTMSK=0x000004ff
```

Check that the following host-channel events are enabled:

- `XFRC`
- `CHH`
- `STALL`
- `NAK`
- `ACK`
- `TXERR`
- `BBERR`
- `DTERR`
- `AHBERR`

Also verify the global masks:

```c
GINTMSK.HCINT
GINTMSK.RXFLVL
GINTMSK.NPTXFEMP
GINTMSK.PRTIM
```

For a minimal non-DMA control transfer, make sure:

- DMA is disabled.
- `RXFLVL` is enabled if the HAL expects FIFO-level servicing.
- `NPTXFEMP` behavior is consistent with the HAL.
- the IRQ is not being serviced by both Zephyr and a manually installed handler.

A particularly useful experiment is to run the first transfer with DMA definitively disabled and use only the slave/non-DMA path. This removes cache and DMA buffer issues from the test.

## Check the STM32 HAL version and patches

The repository uses vendored STM32H7 HAL code. Compare these functions against the exact upstream version used by the known-working H755 host project mentioned in the document:

- `USB_HostInit()`
- `USB_HC_Init()`
- `USB_HC_StartXfer()`
- `USB_ReadPacket()`
- `USB_WritePacket()`
- `HAL_HCD_HC_SubmitRequest()`
- `HAL_HCD_IRQHandler()`

Do not compare only the high-level USB Host Core. Both the custom stack and ST USBH fail, so the common HAL/LL implementation and its integration are the likely shared layer.

In particular, check whether the H7 HAL expects the host interrupt handler to call:

```c
HAL_HCD_IRQHandler(&hhcd_USB_OTG_FS);
```

from the actual vector and whether the adapter has overridden any weak callback in a way that changes `hhcd->hc[ch].state`, `urb_state`, or `toggle_in/toggle_out`.

## Suggested next experiment order

1. Change the first EP0 request to MPS 8 and length 8.
2. Dump the setup-buffer words and verify they are written to the TX FIFO.
3. Capture `HCINT`, `HAINT`, and `GINTSTS` immediately after channel enable and inside the IRQ handler.
4. Confirm DMA is disabled and the non-DMA FIFO path is active.
5. Compare `USB_HC_StartXfer()` and `HAL_HCD_IRQHandler()` with the HAL version used by the working H755 host report.
6. Test with a simple low-speed/full-speed mouse or keyboard, but retain the same diagnostic capture.
7. Only after a valid first descriptor transfer works, continue with address assignment and configuration.

The current evidence does **not** support spending more time on PD10, JP8, ID grounding, VBUS, PA9, AF10, or the board’s D+/D− hardware. The next decisive question is: **what exact bytes reach the host TX FIFO, and what exact `HCINT` event occurs immediately after the channel is enabled?**
