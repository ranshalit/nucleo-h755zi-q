# Speech AI per-attempt peak scores

## Goal

Expose the highest model score for each supported keyword during a candidate
attempt, including scores below the active detection threshold. Preserve the
existing accepted-command logic, threshold controls, and dashboard actions.
Scores are model outputs, not calibrated probabilities.

## Attempt boundaries and capture

Track peaks on the board at every inference, not from the browser's slower
polling. A candidate attempt starts when a keyword is the winning model class,
regardless of its score or the detection threshold. During an active attempt,
capture the maximum score of every keyword (`yes`, `no`, `up`, `down`, `left`,
`right`, `stop`), including frames where `silence` or `unknown` wins.

End an attempt after `silence` or `unknown` remains the winning class for
300 ms. Include these trailing inference frames in the peak calculation.
Silence never replaces the displayed word or erases completed peaks. Preserve
the completed attempt until another attempt completes.

These are model candidate episodes, not guaranteed spoken-word boundaries.
Repeated words without a silence/unknown break may merge. An utterance
classified entirely as silence/unknown will not start an attempt. Independent
audio-activity detection would address that limitation, but requires microphone
noise-floor calibration and is outside this first change; do not claim that
every spoken utterance is captured.

## Board state and API

Use bounded, fixed-size live and last-attempt records in the thread-safe speech
snapshot. Each record contains a sequence identifier, per-keyword peak scores,
start/end board uptime, and per-keyword acceptance flags. Live records have no
end timestamp until finalized. Report whether the last attempt completed
normally or was interrupted.

Update records alongside inference results, before acceptance filtering.
Acceptance flags come only from the existing decision logic: crossing the
threshold once does not imply acceptance. Changing the threshold does not
reset peaks or generate commands.

Extend `GET /api/v1/speech` with live and last-attempt records. Preserve existing
fields and explicitly represent absent attempts. Serialization must remain
bounded and report insufficient output capacity rather than truncating.
Resize the HTTP response buffer to cover the extended response.

A new capture session clears both records. Audio discontinuities and model
errors finalize any live record as interrupted, retaining captured scores;
they must not produce a normal completed attempt or fabricated predictions.
Keep completed records visible during temporary warming-up or error states
within the same session, clearly marked as historical.

## Dashboard

Keep the current live model class and score. Add separate **Live attempt** and
**Last attempt** areas, showing the strongest keyword prominently and all
keyword peaks in compact detail. Example:

`LEFT | Peak 67.4% | Threshold 80% | 12.6 percentage points below | Not accepted`

Compare scores against the currently confirmed active threshold, not an
unapplied input value. Recompute the difference when the threshold changes.
An above-threshold peak without an accepted event says **Above threshold,
not accepted**, never **Detected**.

Briefly highlight a newly observed increase in a keyword's live peak. Keep
the number visible after the highlight ends. Use amber for unaccepted candidates
and a green acceptance badge only for actual accepted commands; text conveys
the meaning independently of color. Respect reduced-motion preferences.
Polling the same values again must not replay highlights.

Silence may change the current live class, but cannot hide the separate attempt
records. A completed attempt remains visible while the next live attempt runs.
On polling failure, identify retained values as stale rather than current.
No new checkbox, persistent storage, inference, or command behavior is needed.

## Verification

Add focused host-side tests for below-threshold starts, all-keyword peak capture,
silence/unknown trailing frames, 300 ms finalization, repeated polls, independent
live and last records, competing keywords, accepted-event attribution, session
resets, and interruption handling.

Extend JSON tests for absent/live/completed/interrupted records, score bounds,
output shape, and insufficient buffer capacity. Extend dashboard tests for
persistent completed scores, threshold differences, above-threshold-but-rejected
labels, highlight deduplication, stale/error handling, and reduced motion.

Run the existing targeted speech and web tests and build the M7 application
with the Zephyr SDK when available. Hardware confirmation should include a
below-threshold spoken attempt and a short peak between browser polls.
Report hardware validation as unavailable if no board verification is performed.
