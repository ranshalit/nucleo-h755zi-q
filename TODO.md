# TODO

## Speech model noise rejection

- [ ] Resume investigation documented in [docs/model_debug.md](docs/model_debug.md).
  Experimental six-keyword model still accepts "shhhh" and mic-contact noise
  as commands. Room-noise retraining reduced, but did not eliminate, false events.
- [ ] Collect separately recorded negative examples and keyword positive controls;
  verify offline replay matches the board before using it to select changes.
- [ ] Evaluate explicit noise negatives and score/cadence tradeoffs against
  false-trigger rate, keyword recall, and latency targets. Preserve runtime
  threshold settings; do not solve rejection by blindly raising the threshold.
- [ ] Investigate historical audio gaps/drops under speech/network load.
- [ ] Improve rejection without regressing the v1 direction demo or runtime
  threshold controls. V1 is committed; v2/checkpoints are backed up locally.

Direction support is being integrated using v1, not the room-noise v2 model.
Both models still have known false triggers; integration is not a noise fix.

## USB host workaround cleanup

- [ ] Create a separate cleanup branch/worktree. Keep the validated event-worker
  implementation on master unchanged until hardware trials pass.
- [ ] Remove post-reset jitter in `usbh_core.c`; retain the vendor's 100 ms delay.
- [ ] Remove the 5-54 ms control-error retry jitter in `usbh_ctlreq.c`.
- [ ] Restore `USBH_MAX_ERROR_COUNT` from 10 to the vendor default of 2.
- [ ] Replace fixed 5 ms pause waits in `rawtest` and `vbuscycle` with acknowledged
  worker quiescence, following the existing quiescence helper.
- [ ] Correct stale polling and timing comments.

Change one behavior at a time. After each change, validate mouse and microphone
reset/reconnect, then microphone streaming under speech and network load. Retain
each removal only if behavior remains healthy; redundancy of the jitter and
retry changes has not yet been demonstrated.

Preserve the PHY sleep-clock fix, microphone ISR resubmission, RX-before-completion
handling, and optional polling comparator.
