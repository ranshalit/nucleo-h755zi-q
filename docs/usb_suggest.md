Yes. I read the **current `docs/usb_debug.md` all the way through the latest Host-mode investigation**, and I think the investigation has now narrowed the problem considerably.

The most important result is this:

> **The STM32H755 + CN13 + cable + camera + OTG_FS PHY can perform the exact SETUP transaction successfully under bare-metal.**
> The same transaction fails under Zephyr.
> Therefore I would stop changing USB electrical configuration, ID, VBUS, clocks, FIFO sizes, or enumeration state-machine logic.

The bare-metal test gets `HCINT=0x21` (`XFRC | ACK`) immediately, while Zephyr gets `TXERR`/`Device not responding`. 

### What I would investigate next

I think there is one particularly interesting clue in your latest code.

In `stm32_hcd_adapter.c`, your Zephyr implementation does:

```c
static void stm32_hcd_isr(const void *arg)
{
    ARG_UNUSED(arg);
    HAL_HCD_IRQHandler(&hcd_handle);
}
```

and connects it with:

```c
IRQ_CONNECT(STM32_HCD_IRQ, STM32_HCD_IRQ_PRI, stm32_hcd_isr, NULL, 0);
irq_enable(STM32_HCD_IRQ);
```

with:

```c
#define STM32_HCD_IRQ_PRI 5
```



The bare-metal version, by contrast, has **no OTG interrupt at all**. It simply starts the channel and polls the hardware. Yet that exact sequence succeeds.

You already tried disabling the USB IRQ around the `rawtest`, but that doesn't completely reproduce the bare-metal environment because **all the other Zephyr interrupts and scheduler activity remain active**. The document correctly notes this distinction. 

However, there is an even more useful experiment I would do before spending time on NVIC priorities:

## 1. Make a truly synchronous Zephyr HCD test

Not the current `pollmode`.

Instead, create a test where **everything from `USB_HC_Init()` through `USB_HC_StartXfer()` and completion happens inside one uninterrupted `k_thread` execution**, with:

* OTG IRQ disabled
* no `USBH_Process()`
* no other USBH thread
* no callbacks
* no `printf()` during the critical interval
* no `k_msleep()`
* no `k_yield()`
* use `k_busy_wait()` or preferably the DWT cycle counter for the timeout
* poll only `HCINT`, `HCCHAR`, `HCTSIZ`

Essentially reproduce the successful bare-metal code **inside the Zephyr application**, but don't use the USBH middleware at all.

Your existing `rawtest` is very close, but it still contains a huge amount of diagnostic machinery before the transfer. In particular, the current function performs things such as the HFNUM/SOF tests, HFIR manipulation, Ethernet clock experiments, etc. before reaching the actual transfer. 

I would create something brutally small:

```text
usbhost minitest
    |
    +-- disable OTG IRQ
    |
    +-- HPRT reset
    |
    +-- USB_HC_Init(ch=1)
    |
    +-- prepare setup packet
    |
    +-- USB_HC_StartXfer()
    |
    +-- tight polling loop
          |
          +-- HCINT
          +-- HCCHAR
          +-- HCTSIZ
          |
          +-- stop on XFRC / ACK / TXERR / STALL / CHH
```

No USBH library whatsoever.

### Why this test is important

You currently have:

**Bare metal**

```text
USB_HC_Init
    ↓
USB_HC_StartXfer
    ↓
HCINT = XFRC + ACK
```

**Zephyr + USBH middleware**

```text
USBH_Process
    ↓
USBH_LL_SubmitURB
    ↓
HAL_HCD_HC_SubmitRequest
    ↓
TXERR / Device not responding
```

The missing experiment is:

**Zephyr + absolutely minimal HAL**

```text
Zephyr
    ↓
USB_HC_Init
    ↓
USB_HC_StartXfer
    ↓
poll
```

If **that succeeds**, then we know the HAL itself is fine under Zephyr and the problem is definitely introduced by the USBH integration.

If **that fails**, then we have something much more fundamental about the Zephyr runtime environment.

---

# 2. There is a very suspicious timing difference

The latest document says the raw test sometimes succeeds and sometimes fails, and changing the delay between invocations changes **which invocation fails**. That's a very strong clue. 

That suggests something periodic.

But I would not jump immediately to "IRQ latency."

Instead, instrument the **exact number of CPU cycles between these events**:

```text
HPRT reset complete
        ↓
USB_HC_Init()
        ↓
USB_HC_StartXfer()
        ↓
first HCCHAR read
        ↓
first HCINT
```

For example:

```text
T0 = DWT->CYCCNT
USB_HC_Init()
T1

USB_HC_StartXfer()
T2

poll HCCHAR
T3

poll HCINT
T4
```

Then print only after the transaction has finished.

Do this for both:

* successful rawtest
* failing rawtest

You may discover something like:

```text
SUCCESS:
StartXfer → first hardware observation = 200 cycles

FAIL:
StartXfer → first observation = 80,000 cycles
```

or, more interestingly:

```text
SUCCESS:
StartXfer occurs immediately after SOF

FAIL:
StartXfer occurs at a particular HFNUM phase
```

Your own experiments already strongly suggest **phase sensitivity**. 

---

# 3. Test `ODDFRM` explicitly

This is one thing I would elevate substantially.

Your current channel initialization ultimately programs `HCCHAR`, and the latest debug notes specifically mention:

> `HCCHAR.ODDFRM`

as one of the remaining possibilities. 

For a full-speed host controller, transaction scheduling is tied to the frame/odd-even scheduling machinery.

I would capture:

```text
HFNUM
HCCHAR
HCTSIZ
HNPTXSTS
HCINT
HCINTMSK
HAINT
HAINTMSK
GINTSTS
GINTMSK
```

**immediately before `USB_HC_StartXfer()` and immediately after it.**

Especially compare:

```text
HCCHAR before
HCCHAR after
```

including:

* CHENA
* CHDIS
* ODDFRM
* EPDIR
* EPTYP
* MPS
* EPNUM
* DAD

Your current dump doesn't make the `HCCHAR` bit-level transition sufficiently explicit.

---

# 4. I would also test the "first transaction after reset" timing

This is probably the highest-value USB-specific experiment.

The successful bare-metal sequence is:

```text
VBUS
 ↓
200 ms
 ↓
HPRT reset
 ↓
20 ms
 ↓
HC init
 ↓
StartXfer
 ↓
ACK
```

Your production USBH path is more complicated.

So make a deterministic test with:

```text
VBUS ON
wait 200 ms

HPRT reset
wait 20 ms

read HFNUM

wait until a known HFNUM boundary

USB_HC_Init()

wait until next SOF

USB_HC_StartXfer()
```

Then repeat while deliberately changing the phase:

```text
SOF + 0 us
SOF + 50 us
SOF + 100 us
SOF + 150 us
...
```

If success/failure correlates with phase, you've essentially found the mechanism.

---

# 5. One thing I would change immediately: don't modify `HFIR` in production diagnostics

Your current `rawtest` does:

```c
host_regs->HFIR = 60000U;
```

even though this is intended as an experiment. 

That's fine for a diagnostic experiment, but I would remove it from the normal test path now.

You've already established that:

* 48000 vs 60000 isn't the explanation
* SOF really exists
* static register configuration matches bare-metal

So continuing to perturb HFIR makes later measurements harder to interpret.

---

# 6. The most interesting architectural test: remove `USBH_LL_IncTimer()`

There is another experiment I'd make.

Your SOF callback does:

```c
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
    if (hhcd->pData != NULL) {
        USBH_LL_IncTimer(hhcd->pData);
    }
}
```



ST's USBH middleware relies on the SOF callback for its internal timer.

Temporarily make:

```c
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
    ARG_UNUSED(hhcd);
}
```

and run **only the raw HAL test**, not enumeration.

If rawtest behavior changes, you've found a very unusual interaction between the USB HCD interrupt path and USBH's timer machinery.

If it doesn't, immediately restore it.

---

# 7. Compare interrupt priority with ST's reference

Your current:

```c
#define STM32_HCD_IRQ_PRI 5
```

The ST reference apparently uses NVIC priority 6. Your own document already identified this as untested. 

I'd test three builds:

```text
priority 3
priority 5   ← current
priority 6   ← ST reference
```

and perhaps:

```text
priority 7
```

But don't interpret a success/failure change as proof yet. The important observation would be:

```text
priority 5 → 0/10
priority 6 → 10/10
```

That would strongly implicate interrupt scheduling.

Also dump the actual NVIC priority register at runtime rather than trusting the macro.

---

# 8. Even better: disable every interrupt except SysTick

You already tested:

```text
USB IRQ disabled
```

but that's not the same experiment.

I would temporarily make a diagnostic firmware where, immediately before `USB_HC_StartXfer()`:

```text
disable OTG IRQ
disable Ethernet IRQ
disable I2C IRQs
disable timers
disable GPIO IRQs
disable everything except whatever is absolutely required
```

Then execute the transaction.

If it suddenly becomes 100% reliable, progressively re-enable interrupts:

```text
OTG only
OTG + SysTick
OTG + SysTick + Ethernet
...
```

That gives you a binary search for the offending subsystem.

This is much more informative than simply changing USB IRQ priority.

---

# 9. One potentially important observation from your current code

Your `USBH_LL_SubmitURB()` calls:

```c
HAL_HCD_HC_SubmitRequest(...)
```

directly. 

I would instrument **inside `USB_HC_StartXfer()` itself**, not only before/after `HAL_HCD_HC_SubmitRequest()`.

Specifically record:

```text
HCCHAR before
HCTSIZ before

USB_HC_StartXfer()

HCCHAR immediately after
HCTSIZ immediately after
```

and ideally temporarily modify a local copy of the HAL so it records the exact register writes:

```text
HCCHAR write #1 = ...
HCTSIZ write    = ...
HCCHAR write #2 = ...
```

because the most interesting unexplained observation in the entire log is that earlier you saw:

```text
CHENA = 1
CHDIS = 1
```

even though `USB_HC_StartXfer()` itself clears `CHDIS` before setting `CHENA`. 

That suggests that **something after the HAL writes HCCHAR is modifying channel state**, or the hardware is reacting to a condition in a way that is timing-dependent.

---

## My recommended order

I would do these in exactly this order:

| Test                                                          | What it isolates              |
| ------------------------------------------------------------- | ----------------------------- |
| **A. Minimal Zephyr HAL-only synchronous transaction**        | USBH middleware vs Zephyr/HAL |
| **B. DWT timestamps around `StartXfer`**                      | timing/phase difference       |
| **C. Dump HCCHAR/HCTSIZ/HNPTXSTS before + immediately after** | channel scheduling            |
| **D. Sweep transaction start phase relative to SOF**          | periodic timing collision     |
| **E. Disable all interrupts temporarily**                     | Zephyr interrupt interference |
| **F. NVIC priority 3/5/6/7 sweep**                            | interrupt priority            |
| **G. Remove `USBH_LL_IncTimer()` temporarily**                | USBH SOF/timer interaction    |

### And I would **not** spend time on these anymore

You've actually done a very good job eliminating them:

* PA10 / ID grounding
* PD10 polarity
* JP8
* SB21/SB26/SB27/SB29 population
* VBUS availability
* USB33 supply
* HSI48 selection
* 480 MHz vs 400 MHz CPU clock
* FIFO sizing
* HFIR value
* PCGCCTL
* SOF existence
* Ethernet clock contention
* CM4
* cache/MPU
* port-reset duration
* USB device stack conflict
* custom enumeration algorithm
* ST USBH vs custom enumerator

The bare-metal A/B test is particularly powerful because it proves that the **same physical setup and same ST HCD primitives can produce `XFRC+ACK` on this exact board**. 

So at this point I'd stop treating this as a generic "STM32 USB Host bring-up" problem. It has become a **very specific Zephyr-vs-bare-metal runtime differential**.

The single experiment I'd prioritize is **A: a tiny Zephyr `usbhost minitest` that executes only `HPRT reset → HC_Init → StartXfer → tight poll`, with every nonessential interrupt disabled**. If that passes, we can then reintroduce Zephyr components one at a time and find the exact thing that breaks the transaction.

Useful next checks

* Write the minimal Zephyr HAL-only test
* Instrument HCCHAR and HCTSIZ writes
* Add DWT timing instrumentation

