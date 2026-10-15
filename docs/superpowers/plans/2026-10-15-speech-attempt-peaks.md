# Speech Attempt Peaks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve per-attempt keyword peak scores below the acceptance threshold and display live and completed attempts without losing them to silence.

**Architecture:** A pure C attempt tracker runs on every board inference, independently of command acceptance. The speech snapshot carries fixed-size live and last records, JSON exposes them, and the existing dashboard polls and renders persistent peak labels.

**Tech Stack:** C11, Zephyr, embedded HTML/CSS/JavaScript in `web_assets.c`, host assertion tests, Puppeteer.

## Global Constraints

- Approved spec: `docs/superpowers/specs/2026-10-15-speech-attempt-peaks-design.md`.
- End an attempt after `silence` or `unknown` remains the winning class for 300 ms.
- Capture all seven keyword peaks during an active attempt, including trailing silence/unknown frames.
- Silence never erases completed peaks.
- Keep command acceptance, LEDs, joystick, and threshold behavior unchanged.
- Audio gaps/errors interrupt attempts; new capture sessions clear attempts.
- Model-based episodes cannot capture utterances classified entirely as silence/unknown.
- Scores are model outputs, not calibrated probabilities.
- No new dependencies, dynamic board allocation, audio-activity detection, or automatic board flashing.
- Preserve unrelated untracked `docs/usb_suggest*.md` files.

## File map

| File | Responsibility |
| --- | --- |
| `apps/zephyr-hello-shell/src/speech/speech_attempt.h` | Pure tracker types and interfaces; no dependency on service header |
| `apps/zephyr-hello-shell/src/speech/speech_attempt.c` | Per-inference peaks and attempt lifecycle |
| `apps/zephyr-hello-shell/src/speech/speech_service.h` | Snapshot includes tracker records |
| `apps/zephyr-hello-shell/src/speech/speech_service.c` | Locked tracking, acceptance attribution, interruption/reset |
| `apps/zephyr-hello-shell/src/speech/speech_json.c` | Bounded serialization and record validation |
| `apps/zephyr-hello-shell/src/http_server.c` | Extended speech response capacity |
| `apps/zephyr-hello-shell/src/web_assets.c` | Persistent attempt UI and peak highlights |
| `apps/zephyr-hello-shell/CMakeLists.txt` | Compile pure tracker with service on all targets |
| `apps/zephyr-hello-shell/tests/speech/test_speech_attempt.c` | Tracker lifecycle regression tests |
| `apps/zephyr-hello-shell/tests/speech/test_speech_json.c` | Extended API tests |
| `apps/zephyr-hello-shell/tests/web/test_speech_card.cjs` | Existing speech UI regression tests plus peak coverage |
| `scripts/test_zephyr_speech.sh` | Run new pure C test |
| `README.md` | API fields, display semantics, limitations, validation |

### Task 1: Board-side attempt tracking

**Interfaces:** Define these types in `speech_attempt.h`, include it from
`speech_service.h`, and add `struct speech_attempt_tracker attempts;` to
`struct speech_snapshot`. Seven-element arrays follow label order
`yes/no/up/down/left/right/stop`; map label to index with `label - SPEECH_YES`.

```c
#ifndef APP_SPEECH_ATTEMPT_H
#define APP_SPEECH_ATTEMPT_H
#include <stdbool.h>
#include <stdint.h>
#define SPEECH_KEYWORD_COUNT 7
struct speech_attempt {
    bool valid;
    bool interrupted;
    uint32_t sequence;
    int64_t start_ms;
    int64_t end_ms;
    uint16_t peaks[SPEECH_KEYWORD_COUNT];
    bool accepted[SPEECH_KEYWORD_COUNT];
};
struct speech_attempt_tracker {
    struct speech_attempt live;
    struct speech_attempt last;
    uint32_t sequence;
    int64_t quiet_since;
};
void speech_attempt_reset(struct speech_attempt_tracker *tracker);
void speech_attempt_interrupt(struct speech_attempt_tracker *tracker, int64_t now);
void speech_attempt_update(struct speech_attempt_tracker *tracker,
                          const uint16_t scores[], unsigned int winner,
                          int64_t now, bool accepted, unsigned int event);
#endif
```

- [x] Add failing `test_speech_attempt.c` with these lifecycle assertions:

```c
#include <assert.h>
#include <stdio.h>
#include "speech_service.h"
int main(void)
{
    struct speech_attempt_tracker t;
    uint16_t scores[SPEECH_LABEL_COUNT] = {0};
    speech_attempt_reset(&t);
    scores[SPEECH_LEFT] = 674;
    speech_attempt_update(&t, scores, SPEECH_LEFT, 100, false, SPEECH_UNKNOWN);
    assert(t.live.valid && t.live.peaks[SPEECH_LEFT - SPEECH_YES] == 674);
    scores[SPEECH_SILENCE] = 900;
    scores[SPEECH_LEFT] = 710;
    speech_attempt_update(&t, scores, SPEECH_SILENCE, 200, false, SPEECH_UNKNOWN);
    speech_attempt_update(&t, scores, SPEECH_UNKNOWN, 400, false, SPEECH_UNKNOWN);
    assert(t.live.valid && !t.last.valid);
    speech_attempt_update(&t, scores, SPEECH_SILENCE, 500, false, SPEECH_UNKNOWN);
    assert(!t.live.valid && t.last.valid && t.last.peaks[4] == 710);
    uint32_t previous = t.last.sequence;
    scores[SPEECH_NO] = 950;
    speech_attempt_update(&t, scores, SPEECH_NO, 600, true, SPEECH_NO);
    assert(t.last.sequence == previous && t.live.accepted[1]);
    speech_attempt_interrupt(&t, 700);
    assert(t.last.interrupted && !t.live.valid);
    speech_attempt_reset(&t);
    assert(!t.live.valid && !t.last.valid);
    speech_attempt_update(&t, scores, SPEECH_SILENCE, 800, false, SPEECH_UNKNOWN);
    assert(!t.live.valid);
    puts("PASS: attempt peaks and lifecycle");
}
```

- [x] Add its runner after the decision test in `scripts/test_zephyr_speech.sh`:

```bash
cc -std=c11 -Wall -Wextra -Werror -I"${APP}/src/speech" -I"${APP}/src/usb_host" \
  "${APP}/tests/speech/test_speech_attempt.c" "${APP}/src/speech/speech_attempt.c" \
  -o "${BUILD}/test_attempt"
"${BUILD}/test_attempt"
```

- [x] Run `scripts/test_zephyr_speech.sh`; expect missing header/source failure.
- [x] Implement tracker with these function bodies in `speech_attempt.c`,
  including `speech_service.h` and `string.h`:

```c
void speech_attempt_reset(struct speech_attempt_tracker *t)
{
    memset(t, 0, sizeof(*t));
    t->quiet_since = -1;
}
static void finish(struct speech_attempt_tracker *t, int64_t now, bool interrupted)
{
    t->live.end_ms = now;
    t->live.interrupted = interrupted;
    t->last = t->live;
    memset(&t->live, 0, sizeof(t->live));
    t->quiet_since = -1;
}
void speech_attempt_interrupt(struct speech_attempt_tracker *t, int64_t now)
{
    if (t->live.valid) { finish(t, now, true); }
}
void speech_attempt_update(struct speech_attempt_tracker *t,
                          const uint16_t scores[], unsigned int winner,
                          int64_t now, bool accepted, unsigned int event)
{
    bool keyword = winner >= SPEECH_YES && winner < SPEECH_LABEL_COUNT;
    if (!t->live.valid) {
        if (!keyword) { return; }
        t->live.valid = true;
        if (++t->sequence == 0) { ++t->sequence; }
        t->live.sequence = t->sequence;
        t->live.start_ms = now;
        t->quiet_since = -1;
    }
    for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
        if (scores[SPEECH_YES + i] > t->live.peaks[i]) {
            t->live.peaks[i] = scores[SPEECH_YES + i];
        }
    }
    if (accepted && event >= SPEECH_YES && event < SPEECH_LABEL_COUNT) {
        t->live.accepted[event - SPEECH_YES] = true;
    }
    if (keyword) { t->quiet_since = -1; }
    else {
        if (t->quiet_since < 0) { t->quiet_since = now; }
        if (now - t->quiet_since >= 300) { finish(t, now, false); }
    }
}
```

- [x] Compile tracker unconditionally in the existing common `target_sources`.
  Assert `SPEECH_LABEL_COUNT - SPEECH_YES == SPEECH_KEYWORD_COUNT` in the service.
  No tracker work executes in capture callbacks except bounded reset/interruption.
- [x] Integrate under the existing spinlock: call
  `speech_attempt_interrupt(&status.attempts, k_uptime_get());` from
  `invalidate_locked()`. In `capture_changed_locked`, remember whether
  `input.session != capture->session` before assigning `input`; after invalidation,
  call `speech_attempt_reset(&status.attempts)` only for a new session.
- [x] After `speech_decision_update`, use the same inference timestamp for
  decision, event, and tracker:

```c
int64_t now = k_uptime_get();
accepted = speech_decision_update(&decision, scores, now, &event);
speech_attempt_update(&status.attempts, scores, winner, now, accepted, event);
```

- [x] Extend tracker assertions to cover a short silence break that cancels on
  renewed keyword activity, competing keywords without a break, peaks not
  decreasing, acceptance flags independent of peaks, empty interruption, and a
  new attempt not inheriting previous maxima. Extend existing worker fixture
  tests to observe records through `speech_snapshot_get` on gaps and reconnect.
- [x] Run `scripts/test_zephyr_speech.sh`; expect all pure C tests to pass.
- [x] Commit only Task 1 files with subject
  `feat(speech): retain per-attempt keyword peaks` and required Copilot trailer.

### Task 2: Extend bounded speech JSON

**Interfaces:** JSON gains `live_attempt` and `last_attempt`. Each is `null` or:

```json
{"sequence":1,"start_ms":100,"end_ms":null,"interrupted":false,
 "keywords":[{"label":"yes","peak":0.674,"accepted":false}]}
```

Always serialize seven keyword entries in fixed label order. Completed
`end_ms` is an integer; live `end_ms` is null. Existing fields remain unchanged.

- [x] Extend `test_speech_json.c` buffer to 3072. Put the null assertions after
  the first serialization, and the remaining assertions before existing
  invalid-input tests while the snapshot is valid:

```c
assert(strstr(buffer, "\"live_attempt\":null"));
assert(strstr(buffer, "\"last_attempt\":null"));
snapshot.attempts.live = (struct speech_attempt){
    .valid = true, .sequence = 1, .start_ms = 100,
    .peaks = {674, 0, 0, 0, 0, 0, 0}
};
assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) > 0);
assert(strstr(buffer, "\"end_ms\":null"));
assert(strstr(buffer, "\"label\":\"yes\",\"peak\":0.674,\"accepted\":false"));
snapshot.attempts.live.peaks[0] = 1001;
assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
```

- [x] Run `scripts/test_zephyr_speech.sh`; expect missing JSON fields.
- [x] Add a serialization helper reusing `append`; pass the existing label table:

```c
static int emit_attempt(char *out, size_t capacity, size_t *offset,
                        const struct speech_attempt *a, bool live,
                        const char *const labels[])
{
    if (!a->valid) { return append(out, capacity, offset, "null"); }
    if (!a->sequence || a->start_ms < 0 ||
        (live && (a->interrupted || a->end_ms != 0)) ||
        (!live && a->end_ms < a->start_ms)) { return -EINVAL; }
    for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
        if (a->peaks[i] > 1000) { return -EINVAL; }
    }
    int ret;
#define PART(...) do { ret = append(out, capacity, offset, __VA_ARGS__); \
                      if (ret) { return ret; } } while (0)
    PART("{\"sequence\":%u,\"start_ms\":%lld,\"end_ms\":",
         a->sequence, (long long)a->start_ms);
    if (live) { PART("null"); }
    else { PART("%lld", (long long)a->end_ms); }
    PART(",\"interrupted\":%s,\"keywords\":[", a->interrupted ? "true" : "false");
    for (unsigned int i = 0; i < SPEECH_KEYWORD_COUNT; ++i) {
        PART("%s{\"label\":\"%s\",\"peak\":%u.%03u,\"accepted\":%s}",
             i ? "," : "", labels[SPEECH_YES + i],
             a->peaks[i] / 1000, a->peaks[i] % 1000,
             a->accepted[i] ? "true" : "false");
    }
    PART("]}");
#undef PART
    return 0;
}
```

- [x] Remove the closing `}` from the existing statistics `EMIT` and append:

```c
EMIT(",\"live_attempt\":");
ret = emit_attempt(out, capacity, &offset, &snapshot->attempts.live, true, labels);
if (ret) { return ret; }
EMIT(",\"last_attempt\":");
ret = emit_attempt(out, capacity, &offset, &snapshot->attempts.last, false, labels);
if (ret) { return ret; }
EMIT("}");
```

- [x] Change only `speech_handler` response from 768 to 3072 bytes.
  Extend JSON assertions to completed/interrupted records, int64 timestamps,
  all seven entries, invalid records, maximum scores/counters and exact capacity:
  with a valid snapshot, size `n + 1` succeeds and size `n` returns `-ENOSPC`.
- [x] Run `scripts/test_zephyr_speech.sh`; expect all tests to pass.
- [x] Commit Task 2 files with subject `feat(speech): expose attempt peaks in API`
  and required Copilot trailer.

### Task 3: Persistent dashboard display

**Interfaces:** Dashboard consumes `live_attempt`/`last_attempt`, confirmed
threshold, and session. New DOM IDs: `speech-live-attempt`, `speech-last-attempt`,
`speech-attempt-status`. Existing voice-event logic is untouched.

- [x] Extend `test_speech_card.cjs` fixture after existing threshold tests,
  explicitly restore threshold 80, and add a failing persistent-record test:

```js
const keywords = ['yes', 'no', 'up', 'down', 'left', 'right', 'stop'];
const attempt = {
  sequence: 1, start_ms: 100, end_ms: 500, interrupted: false,
  keywords: keywords.map(label => ({
    label, peak: label === 'left' ? .674 : .01, accepted: false
  }))
};
speech = {...speech, state: 'running', label: 'silence', score: .9,
  threshold_percent: 80, live_attempt: null, last_attempt: attempt};
await page.waitForFunction(() =>
  document.getElementById('speech-last-attempt')?.textContent.includes('67.4%'));
assert.match(await page.$eval('#speech-last-attempt', n => n.textContent),
  /12.6 percentage points below/);
speech.live_attempt = {...attempt, sequence: 2, start_ms: 600, end_ms: null};
await page.waitForFunction(() =>
  document.getElementById('speech-live-attempt')?.textContent.includes('67.4%'));
assert.match(await page.$eval('#speech-last-attempt', n => n.textContent), /67.4%/);
```

- [x] Run the existing web test:
  `NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs`.
  Expected failure: new attempt DOM not present.
- [x] Insert the following markup after the current model-score line, encoded
  as existing C string literals:

```html
<section class="speech-attempts" aria-label="Keyword peak scores">
  <p id="speech-attempt-status" class="helper-text"></p>
  <h4>Live attempt</h4>
  <div id="speech-live-attempt">No active attempt</div>
  <h4>Last attempt</h4>
  <div id="speech-last-attempt">No completed attempt</div>
</section>
```

- [x] Cache DOM nodes with existing `byId`. Add `speechPeakStatus = null`,
  `speechConfirmedThreshold = 80`, and `speechPeakPrevious = {}` beside speech
  state variables. Add these functions (encoded as C strings):

```js
function renderSpeechAttempt(node, attempt, live, session) {
  if (!attempt) {
    node.textContent = live ? 'No active attempt' : 'No completed attempt';
    return;
  }
  var entries = attempt.keywords;
  if (!Array.isArray(entries) || entries.length !== 7 ||
      entries.some(function (entry, index) {
        return entry.label !== ['yes','no','up','down','left','right','stop'][index] ||
          typeof entry.peak !== 'number' || !Number.isFinite(entry.peak) ||
          entry.peak < 0 || entry.peak > 1 || typeof entry.accepted !== 'boolean';
      }) || !Number.isInteger(attempt.sequence) || attempt.sequence < 1 ||
      !Number.isInteger(attempt.start_ms) || attempt.start_ms < 0 ||
      typeof attempt.interrupted !== 'boolean' ||
      (live ? attempt.end_ms !== null || attempt.interrupted :
        !Number.isInteger(attempt.end_ms) || attempt.end_ms < attempt.start_ms)) {
    throw new Error('Invalid speech attempt response from board');
  }
  var strongest = entries.reduce(function (best, e) {
    return e.peak > best.peak ? e : best;
  }, entries[0]);
  var old = {};
  Array.prototype.forEach.call(node.children, function (child) {
    old[child.getAttribute('data-word')] = child;
  });
  entries.forEach(function (entry) {
    var row = old[entry.label] || document.createElement('p');
    row.setAttribute('data-word', entry.label);
    row.className = 'speech-peak-row';
    row.setAttribute('data-strongest', entry === strongest ? '1' : '0');
    row.setAttribute('data-accepted', entry.accepted ? '1' : '0');
    var peakPercent = Math.round(entry.peak * 1000) / 10;
    var gap = Math.round((speechConfirmedThreshold - peakPercent) * 10) / 10;
    var difference = gap > 0 ? gap.toFixed(1) + ' percentage points below' :
      gap < 0 ? (-gap).toFixed(1) + ' percentage points above' : 'At threshold';
    row.textContent = entry.label.toUpperCase() + ' | Peak ' +
      peakPercent.toFixed(1) + '% | Threshold ' + speechConfirmedThreshold +
      '% | ' + difference + ' | ' +
      (entry.accepted ? 'Accepted' :
        gap <= 0 ? 'Above threshold, not accepted' : 'Not accepted');
    var key = session + ':' + attempt.sequence + ':' + entry.label;
    var previous = speechPeakPrevious[key];
    if (live && entry.peak > 0 && (previous === undefined || entry.peak > previous) &&
        !window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
      row.animate([{backgroundColor: entry.accepted ? '#14532d' : '#78350f'},
        {backgroundColor: 'transparent'}], {duration: 700});
    }
    if (live) { speechPeakPrevious[key] = entry.peak; }
    if (!old[entry.label]) { node.appendChild(row); }
  });
}
function renderSpeechPeaks() {
  if (!speechPeakStatus) { return; }
  renderSpeechAttempt(dom.speechLiveAttempt, speechPeakStatus.live_attempt, true,
    speechPeakStatus.session);
  renderSpeechAttempt(dom.speechLastAttempt, speechPeakStatus.last_attempt, false,
    speechPeakStatus.session);
  setText(dom.speechAttemptStatus,
    speechPeakStatus.last_attempt && speechPeakStatus.last_attempt.interrupted ?
      'Last attempt interrupted; peaks retained' :
      speechPeakStatus.state === 'running' ? 'Model candidate episodes' :
        'Historical peaks; recognition ' + speechPeakStatus.state);
}
```

Before first row creation, clear the empty-placeholder text with
`if (!node.children.length) { node.textContent = ''; }`. Prune
`speechPeakPrevious` on session/live-sequence changes so storage remains bounded
to seven live entries; do not reset it on threshold changes or repeated polls.
Use this code before rendering either record, and declare
`var speechPeakIdentity = null;` alongside the other speech variables:

```js
var live = speechPeakStatus.live_attempt;
var identity = live ? speechPeakStatus.session + ':' + live.sequence : null;
if (identity !== speechPeakIdentity) {
  speechPeakPrevious = {};
  speechPeakIdentity = identity;
}
```

Rendering last attempt must not overwrite a live comparison.

- [x] Wire `renderSpeechPeaks()` on every valid poll, outside the
  `state === 'running'` branch and independent of `clearSpeech()`. Reject malformed
  attempt records through the existing polling catch, label retained records
  stale, and do not fabricate empty success results. Before rendering valid
  response records, store the confirmed threshold only if the existing
  revision guard accepts it. Successful threshold POST updates that confirmed
  value and recomputes labels immediately without replaying animations.
  API failure sets `speech-attempt-status` to
  `Stale peaks: speech status unavailable` while retaining prior rows.
- [x] Add styling:

```css
.speech-attempts { margin-top: 1rem; }
.speech-peak-row { padding: .3rem; font-size: .8rem; color: var(--muted); }
.speech-peak-row[data-strongest="1"] { font-weight: 700; color: var(--text); }
.speech-peak-row[data-accepted="1"] { color: #86efac; }
```

- [x] Extend web assertions for new peak versus repeated poll animation counts,
  reduced motion using `page.emulateMediaFeatures`, above-threshold rejected
  scores, confirmed threshold updates versus dirty inputs, interrupted and
  warming-up retention, stale labels on HTTP failure, reconnect clearing,
  malformed records, and unchanged existing voice actions.
- [x] Document `live_attempt` and `last_attempt` and candidate-episode limitation
  in README speech section. State that silence terminates rather than erases
  attempts, last record stays while next runs, peaks are not accepted commands,
  and threshold difference uses currently confirmed threshold.
- [x] Run `scripts/test_zephyr_speech.sh` and the web command above. Then run
  `scripts/flash_zephyr_hello_shell.sh --build-only`; expect successful M7 build.
  If tools are missing, report exact missing prerequisite rather than claiming
  firmware validation. Do not flash or modify host networking.
- [x] Inspect `git diff --check` and commit only Task 3 files with subject
  `feat(web): show live and last attempt peaks` and required Copilot trailer.

## Review and delivery

Execution adjustment: the added dashboard exceeded M7 flash bank 1 by 1,036
bytes. Place the read-only dashboard JavaScript in the unused portion of the
existing 128 KiB `SPEECH_BANK2` region, after the models, using a target-gated
section attribute in `web_assets.c` and `cmake/speech_bank2.ld`. Preserve normal
placement on simulation/non-speech targets. The existing hex flashing flow
programs both sections; confirm final linker placement and region bounds.

- [x] Confirm every spec requirement maps to Tasks 1-3, especially trailing
  silence capture, independent acceptance flags, interruption, and stale UI.
- [x] Report actual completed behavior, any blocked build/hardware verification,
  and candidate-episode limitation. Hardware spoken-word validation is separate
  from host tests and must not be claimed without observing the board.

## Execution evidence

- `scripts/test_zephyr_speech.sh --model`: pure C tests, seven model fixtures,
  worker gap interruption and session reset passed. Extended simulator deadline
  from 40 to 180 seconds to accommodate the added keyword-feed worker coverage.
- `test_speech_card.cjs`: persistent peaks, below/above/exact-threshold labels,
  highlight deduplication, reduced motion, stale/malformed responses,
  interruption, and existing voice actions passed.
- `test_mic_player.cjs`: existing microphone playback regressions passed.
- M7 build with Zephyr SDK succeeded after the flash placement adjustment:
  FLASH 1,007,604 / 1,048,576 bytes; RAM 471,344 / 524,288 bytes;
  SPEECH_BANK2 95,901 / 131,072 bytes.
- Linker output preserves models at `0x08100000` and places dashboard JavaScript
  immediately after them at `0x0810d288`; the sections do not overlap.
- Normal native application build succeeded with speech unavailable and normal
  read-only script placement.
- Physical board was not flashed; live spoken-word hardware validation was not
  performed. Candidate-episode limitations remain as specified.
