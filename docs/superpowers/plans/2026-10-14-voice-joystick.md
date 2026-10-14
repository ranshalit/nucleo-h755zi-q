# Voice Joystick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `stop` keyword to the on-board speech model and a dashboard joystick (plus YES/NO buttons) driven by accepted speech events.

**Architecture:** Retrain the existing `tiny_conv` keyword model with a ninth class `stop` using `scripts/train_speech_keywords.sh`. Extend the firmware label enum and JSON names. The dashboard (`src/web_assets.c`) gains a browser-only joystick widget inside the Speech AI card that reacts to `last_event` from the existing `/api/v1/speech` poll.

**Tech Stack:** Zephyr (nucleo_h755zi_q M7), TFLite Micro, TensorFlow r2.15 speech_commands training, C11 host tests, Puppeteer browser test.

Spec: `docs/superpowers/specs/2026-10-14-voice-joystick-design.md`.
Approved visual mockup: `~/.copilot/session-state/03408db7-cbd4-4357-b369-63370c286ed1/files/joystick-mock/index.html`.

## Global Constraints

- Class order: `silence, unknown, yes, no, up, down, left, right, stop`.
- Model architecture `tiny_conv`, preprocessing `micro`, window stride 20 ms; int8 input scale `0.10171568393707275`, zero point `-128`; ops only `CONV_2D`, `FULLY_CONNECTED`, `RESHAPE`, `SOFTMAX`.
- Model stays in flash bank 2 (`SPEECH_BANK2`, 128 KB at `0x08100000`).
- Joystick state is browser-only. Firmware keeps no joystick mode.
- Directions latch until the next direction or `stop`. `stop` centers. `yes`/`no` press their button for 700 ms; they do not change direction.
- Events are applied once per key `session:sequence:uptime_ms`. On the first successful poll after page load, an existing `last_event` sets the direction without pressing YES/NO.
- Speech not `running` (or poll error): widget dimmed (`data-disabled="1"`), latched direction kept.
- Speech AI card: labels left, widget right; single column under 720 px.
- Zephyr builds use the Zephyr SDK `arm-zephyr-eabi` toolchain.

---

### Task 1: Retrain the keyword model with `stop`

**Files:**
- Modify: `apps/zephyr-hello-shell/models/speech_keywords.tflite` (regenerated)
- Modify: `scripts/train_speech_keywords.sh:8` (default words)

**Interfaces:**
- Produces: `models/speech_keywords.tflite` with 9 int8 outputs in the Global Constraints order.

`~/kws-train/dataset` contains experimental `extra_room_*.wav` noise, which the script rejects. Use a clean work directory that reuses the existing venv, TensorFlow sources and dataset files via links.

- [ ] **Step 1: Change the default word list**

In `scripts/train_speech_keywords.sh` replace:

```bash
WORDS="${SPEECH_WORDS:-yes,no,up,down,left,right}"
```

with:

```bash
WORDS="${SPEECH_WORDS:-yes,no,up,down,left,right,stop}"
```

- [ ] **Step 2: Prepare a clean work directory**

```bash
W=~/kws-train-stop
mkdir -p "$W"
ln -sfn ~/kws-train/venv "$W/venv"
ln -sfn ~/kws-train/tf-src "$W/tf-src"
[ -d "$W/dataset" ] || cp -al ~/kws-train/dataset "$W/dataset"
rm -f "$W"/dataset/_background_noise_/extra_*.wav
ls "$W"/dataset/_background_noise_
```

Expected: only the stock files (`doing_the_dishes.wav`, `dude_miaowing.wav`, `exercise_bike.wav`, `pink_noise.wav`, `README.md`, `running_tap.wav`, `white_noise.wav`). `~/kws-train` must remain unchanged (hard links; `rm` only unlinks in the copy).

- [ ] **Step 3: Train and export**

```bash
SPEECH_TRAIN_DIR=~/kws-train-stop scripts/train_speech_keywords.sh 2>&1 | tee ~/kws-train-stop/train.log
```

Runs on the local GPU (RTX A1000). Expected tail of output:
- `input [ 1 1960] scale=0.10171568 zero=-128; output [1 9]`
- `ops ['CONV_2D', 'FULLY_CONNECTED', 'RESHAPE', 'SOFTMAX']`
- `int8 test accuracy XX.XX% (N=...)` and per-class lines including `stop`
- `wrote .../models/speech_keywords.tflite (~39000 bytes)`

Also record `grep "Final test accuracy" ~/kws-train-stop/train.log | tail -1`.
Acceptance: int8 accuracy ≥ 80% and `stop` per-class ≥ 75%. If lower, stop and report to the user; do not tune silently.

- [ ] **Step 4: Commit**

```bash
git add scripts/train_speech_keywords.sh apps/zephyr-hello-shell/models/speech_keywords.tflite
git commit -m "build(speech): retrain keyword model with stop" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

(The firmware build fails the label-count check until Task 2. Do not flash between tasks.)

---

### Task 2: Firmware `stop` label

**Files:**
- Modify: `apps/zephyr-hello-shell/src/speech/speech_service.h:12-16`
- Modify: `apps/zephyr-hello-shell/src/speech/speech_model.h:7-8`
- Modify: `apps/zephyr-hello-shell/src/speech/speech_json.c:25-27`
- Test: `apps/zephyr-hello-shell/tests/speech/test_speech_json.c`
- Test: `apps/zephyr-hello-shell/tests/speech/test_speech_decision.c`
- Test: `apps/zephyr-hello-shell/tests/speech/model/src/main.c`, `tests/speech/model/CMakeLists.txt`
- Create: `apps/zephyr-hello-shell/tests/speech/model/fixtures/stop_1000ms.wav`
- Modify: `apps/zephyr-hello-shell/tests/speech/model/fixtures/README.md`

**Interfaces:**
- Consumes: 9-output model from Task 1.
- Produces: `enum speech_label` value `SPEECH_STOP` (= 8), `SPEECH_LABEL_COUNT` = 9, JSON label string `"stop"`, `SPEECH_WORDS` = `"yes/no/up/down/left/right/stop"`.

- [ ] **Step 1: Write failing JSON test**

In `tests/speech/test_speech_json.c` replace the direction loop:

```c
	static const char *const names[] = {"up", "down", "left", "right"};
	for (int i = 0; i < 4; ++i) {
```

with:

```c
	static const char *const names[] = {"up", "down", "left", "right", "stop"};
	for (int i = 0; i < 5; ++i) {
```

and change the final message to `"PASS: snapshot JSON thresholds, nulls, escaping, direction/stop labels and overflow"`.

- [ ] **Step 2: Write failing decision test**

Append to `main()` in `tests/speech/test_speech_decision.c`, before its final `puts`/`return`, a case that `stop` is an accepted event. First read the file to reuse its existing score helper pattern; the added block must be:

```c
	{
		struct speech_decision d;
		enum speech_label ev = SPEECH_SILENCE;
		uint16_t s[SPEECH_LABEL_COUNT] = {0};
		speech_decision_reset(&d);
		s[SPEECH_STOP] = 950;
		bool fired = false;
		for (int64_t t = 0; t <= 400 && !fired; t += 100) {
			fired = speech_decision_update(&d, s, t, &ev);
		}
		assert(fired && ev == SPEECH_STOP);
		puts("PASS: stop is an accepted keyword");
	}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `scripts/test_zephyr_speech.sh`
Expected: compile error `'SPEECH_STOP' undeclared`.

- [ ] **Step 4: Implement labels**

`src/speech/speech_service.h`:

```c
enum speech_label {
	SPEECH_SILENCE, SPEECH_UNKNOWN, SPEECH_YES, SPEECH_NO,
	SPEECH_UP, SPEECH_DOWN, SPEECH_LEFT, SPEECH_RIGHT, SPEECH_STOP,
	SPEECH_LABEL_COUNT
};
#define SPEECH_WORDS "yes/no/up/down/left/right/stop"
```

`src/speech/speech_model.h`:

```c
/* Classifier output order: silence, unknown, yes, no, up, down, left, right, stop. */
#define SPEECH_MODEL_LABELS 9
```

`src/speech/speech_json.c` labels array:

```c
	static const char *const labels[SPEECH_LABEL_COUNT] = {
		"silence", "unknown", "yes", "no", "up", "down", "left", "right", "stop"
	};
```

- [ ] **Step 5: Run host tests**

Run: `scripts/test_zephyr_speech.sh`
Expected: all three binaries print PASS, including `PASS: stop is an accepted keyword` and the updated JSON PASS line.

- [ ] **Step 6: Add the stop fixture**

```bash
cp ~/kws-train/dataset/stop/97f4c236_nohash_2.wav apps/zephyr-hello-shell/tests/speech/model/fixtures/stop_1000ms.wav
grep -c '^stop/97f4c236_nohash_2.wav$' ~/kws-train/dataset/testing_list.txt   # expect 1
```

Append to the fixture table in `fixtures/README.md`:

```markdown
| `stop_1000ms.wav` | `stop/97f4c236_nohash_2.wav` |
```

and change the heading line `# Direction keyword fixtures` to `# Direction and stop keyword fixtures`.

In `tests/speech/model/CMakeLists.txt` change:

```cmake
foreach(fixture up_1000ms down_1000ms left_1000ms right_1000ms)
```

to:

```cmake
foreach(fixture up_1000ms down_1000ms left_1000ms right_1000ms stop_1000ms)
```

In `tests/speech/model/src/main.c` add after the `right_audio` array:

```c
static const uint8_t stop_audio[] = {
#include <stop_1000ms.inc>
};
```

and after `classify(right_audio, sizeof(right_audio), SPEECH_RIGHT);`:

```c
	classify(stop_audio, sizeof(stop_audio), SPEECH_STOP);
```

- [ ] **Step 7: Run model fixture tests**

Run: `ZEPHYR_BASE="$PWD/vendor/zephyrproject/zephyr" scripts/test_zephyr_speech.sh --model`
Expected: `fixture expected=8 winner=8 ...`, `PASS: model fixtures`, `PASS: worker warmup/...`.
If the stop fixture (or any existing fixture) is misclassified, pick a different `stop/*` clip that is listed in `testing_list.txt` and is classified correctly; update README to match. If an existing direction/yes/no fixture fails, stop and report (model regression).

- [ ] **Step 8: Commit**

```bash
git add apps/zephyr-hello-shell/src/speech apps/zephyr-hello-shell/tests/speech
git commit -m "feat(speech): accept stop keyword" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 3: Dashboard voice joystick

**Files:**
- Modify: `apps/zephyr-hello-shell/src/web_assets.c` (HTML Speech AI card ~lines 101-117, CSS before `@media (max-width: 720px)` ~line 473, JS speech section ~lines 1270-1352 and `cacheDom` ~line 1391)
- Test: `apps/zephyr-hello-shell/tests/web/test_speech_card.cjs`

**Interfaces:**
- Consumes: `/api/v1/speech` JSON `{state, label, last_event: {label, sequence, uptime_ms} | null, session, ...}`; labels may include `"stop"`.
- Produces: DOM ids `voice-pad`, `joy-knob` (attr `data-dir` ∈ up/down/left/right/stop), `voice-yes`, `voice-no` (attr `data-pressed` "1"/"0"), `voice-caption` (text `Mode: <dir>`), arrows `.joy-arrow[data-arrow=<dir>]` (attr `data-active`).

All asset text lives in C string literals: every line is `"...\n"` with `"` escaped as `\"`. Keep 4-space indentation of the literals.

- [ ] **Step 1: Write the failing browser test**

In `tests/web/test_speech_card.cjs`:

1. Change `assert.match(... /yes, no, up, down, left, right/)` to `/yes, no, up, down, left, right, stop/`, and the running `reason` to `'recognizing English yes/no/up/down/left/right/stop'`.
2. Add helpers right after `waitText` is defined:

```js
    const attr = (id, name) => page.$eval('#' + id, (n, name) => n.getAttribute(name), name);
    const waitAttr = (id, name, value) => page.waitForFunction((id, name, value) =>
      document.getElementById(id)?.getAttribute(name) === value, {timeout: 5000}, id, name, value);
```

3. Right after `await waitText('speech-state', 'unavailable');` (first one) add:

```js
    await waitAttr('voice-pad', 'data-disabled', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'stop');
```

4. Right after `await waitText('speech-event', 'Detected: yes');` add:

```js
    await waitAttr('voice-pad', 'data-disabled', '0');
    await waitAttr('voice-yes', 'data-pressed', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'stop', 'yes moved joystick');
    await waitAttr('voice-yes', 'data-pressed', '0');
```

5. Right after `await waitText('speech-event', 'Detected: no');` add:

```js
    await waitAttr('voice-no', 'data-pressed', '1');
    await waitAttr('voice-no', 'data-pressed', '0');
```

6. Right after `assert.equal(await page.evaluate(() => window.speechHighlights), 2);` (left event) add:

```js
    assert.equal(await attr('joy-knob', 'data-dir'), 'left');
    assert.equal(await page.$eval('.joy-arrow[data-arrow="left"]', n => n.getAttribute('data-active')), '1');
    await waitText('voice-caption', 'Mode: left');
    speech.last_event = {label: 'up', sequence: 4, uptime_ms: 18000};
    await waitAttr('joy-knob', 'data-dir', 'up');
    speech.last_event = {label: 'yes', sequence: 5, uptime_ms: 20000};
    await waitAttr('voice-yes', 'data-pressed', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'up', 'direction not latched');
    speech.last_event = {label: 'stop', sequence: 6, uptime_ms: 22000};
    await waitAttr('joy-knob', 'data-dir', 'stop');
    await waitText('joy-knob', 'STOP');
    await waitText('voice-caption', 'Mode: stop');
    speech.last_event = {label: 'right', sequence: 7, uptime_ms: 24000};
    await waitAttr('joy-knob', 'data-dir', 'right');
```

7. Right after `await waitText('speech-event', 'No accepted keyword');` (the unplugged case) add:

```js
    await waitAttr('voice-pad', 'data-disabled', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'right', 'latched direction lost');
```

8. Before `assert.equal(ledRequests, 0, ...)` add a page-reload check:

```js
    speech = {state: 'running', reason: 'recognizing', label: 'silence', score: .9,
      last_event: {label: 'down', sequence: 9, uptime_ms: 30000}, session: 3,
      inference_us: 1500, max_processing_us: 2400, threshold_percent: 80};
    const page2 = await browser.newPage();
    await page2.goto(`http://127.0.0.1:${server.address().port}`);
    await page2.waitForFunction(() => document.getElementById('joy-knob')?.getAttribute('data-dir') === 'down', {timeout: 5000});
    speech.last_event = {label: 'yes', sequence: 10, uptime_ms: 32000};
    await page2.waitForFunction(() => document.getElementById('voice-yes')?.getAttribute('data-pressed') === '1', {timeout: 5000});
    await page2.close();
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
```

and change the final log to `'PASS: speech states, direction events, joystick/buttons, threshold updates/errors and unchanged LEDs'`.

Note the initial-load rule (no YES/NO press for a pre-existing event) is covered by the unit-level logic in Step 3 (`speechFirstPoll`); the reload check proves a pre-existing direction is restored and later events still fire.

- [ ] **Step 2: Run test to verify it fails**

Run: `NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs`
Expected: FAIL (timeout waiting for `voice-pad` / null element).

- [ ] **Step 3: Implement HTML**

Replace the Speech AI `<article>` body in `board_http_index_html` (the lines from `<p class=\"helper-text\">English keyword recognition` through `<p id=\"speech-threshold-feedback\" ...></p>`) with:

```c
    "          <div class=\"speech-layout\">\n"
    "            <div>\n"
    "              <p class=\"helper-text\">English keyword recognition (yes, no, up, down, left, right, stop) runs on the STM32, offline. Listening is optional; LEDs stay unchanged.</p>\n"
    "              <p class=\"helper-text\">Current model class (not an accepted command):</p><strong id=\"speech-label\">--</strong>\n"
    "              <p id=\"speech-score\" class=\"helper-text\">Model score: --</p>\n"
    "              <p id=\"speech-event\" class=\"helper-text\" aria-live=\"polite\">No accepted keyword</p>\n"
    "              <p id=\"speech-timing\" class=\"helper-text\">Inference unavailable</p>\n"
    "              <p id=\"speech-health\" class=\"helper-text\">Connect the USB microphone</p>\n"
    "              <div class=\"speech-threshold-controls\">\n"
    "                <label for=\"speech-threshold-input\">Detection threshold (%)\n"
    "                  <input id=\"speech-threshold-input\" type=\"number\" min=\"50\" max=\"100\" step=\"1\" value=\"80\">\n"
    "                </label>\n"
    "                <button id=\"speech-threshold-apply\" class=\"action-button\" type=\"button\">Apply</button>\n"
    "              </div>\n"
    "              <p id=\"speech-threshold-active\" class=\"helper-text\">Active threshold: 80%</p>\n"
    "              <p id=\"speech-threshold-feedback\" class=\"helper-text\" aria-live=\"polite\"></p>\n"
    "            </div>\n"
    "            <div>\n"
    "              <div id=\"voice-pad\" class=\"voice-pad\" data-disabled=\"1\" role=\"group\" aria-label=\"Voice joystick\">\n"
    "                <div class=\"joystick\">\n"
    "                  <span class=\"joy-arrow up\" data-arrow=\"up\" data-active=\"0\">&#9650;</span>\n"
    "                  <span class=\"joy-arrow down\" data-arrow=\"down\" data-active=\"0\">&#9660;</span>\n"
    "                  <span class=\"joy-arrow left\" data-arrow=\"left\" data-active=\"0\">&#9664;</span>\n"
    "                  <span class=\"joy-arrow right\" data-arrow=\"right\" data-active=\"0\">&#9654;</span>\n"
    "                  <div id=\"joy-knob\" class=\"joy-knob\" data-dir=\"stop\">STOP</div>\n"
    "                </div>\n"
    "                <div class=\"voice-buttons\">\n"
    "                  <span id=\"voice-yes\" class=\"voice-btn yes\" data-pressed=\"0\">YES</span>\n"
    "                  <span id=\"voice-no\" class=\"voice-btn no\" data-pressed=\"0\">NO</span>\n"
    "                </div>\n"
    "              </div>\n"
    "              <p id=\"voice-caption\" class=\"voice-caption\" aria-live=\"polite\">Mode: stop</p>\n"
    "            </div>\n"
    "          </div>\n"
```

YES/NO are `<span>` (display-only), so the existing `querySelectorAll('.action-button')` code is unaffected.

- [ ] **Step 4: Implement CSS**

Insert into `board_http_styles_css` immediately after the `.speech-threshold-controls input { ... }` line:

```c
    ".speech-layout { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 1.5rem; align-items: start; }\n"
    ".voice-pad { display: flex; align-items: center; gap: 1.25rem; padding: 1rem; border-radius: 1rem; background: #09101f; border: 1px solid rgba(99, 240, 255, 0.16); transition: opacity 200ms; }\n"
    ".voice-pad[data-disabled=\"1\"] { opacity: 0.35; filter: grayscale(0.7); }\n"
    ".joystick { position: relative; width: 176px; height: 176px; border-radius: 50%; background: radial-gradient(circle, #121c36 0%, #0b1226 70%); border: 2px solid var(--border); box-shadow: inset 0 0 24px rgba(0, 0, 0, 0.6); }\n"
    ".joy-arrow { position: absolute; color: var(--muted); font-size: 1.1rem; line-height: 1; opacity: 0.55; transition: color 150ms, opacity 150ms, text-shadow 150ms; }\n"
    ".joy-arrow[data-active=\"1\"] { color: var(--accent); opacity: 1; text-shadow: 0 0 10px var(--accent); }\n"
    ".joy-arrow.up { top: 8px; left: 50%; transform: translateX(-50%); }\n"
    ".joy-arrow.down { bottom: 8px; left: 50%; transform: translateX(-50%); }\n"
    ".joy-arrow.left { left: 10px; top: 50%; transform: translateY(-50%); }\n"
    ".joy-arrow.right { right: 10px; top: 50%; transform: translateY(-50%); }\n"
    ".joy-knob { position: absolute; left: 50%; top: 50%; width: 64px; height: 64px; margin: -32px 0 0 -32px; border-radius: 50%; display: grid; place-items: center; font-size: 0.7rem; font-weight: 700; letter-spacing: 0.08em; color: var(--muted); background: radial-gradient(circle at 35% 30%, #3a4b7a, #1a2546 70%); border: 2px solid #4a5c90; box-shadow: 0 6px 14px rgba(0, 0, 0, 0.5); transition: transform 220ms cubic-bezier(.3, 1.4, .5, 1), border-color 150ms, color 150ms; }\n"
    ".joy-knob[data-dir=\"up\"] { transform: translateY(-46px); border-color: var(--accent); }\n"
    ".joy-knob[data-dir=\"down\"] { transform: translateY(46px); border-color: var(--accent); }\n"
    ".joy-knob[data-dir=\"left\"] { transform: translateX(-46px); border-color: var(--accent); }\n"
    ".joy-knob[data-dir=\"right\"] { transform: translateX(46px); border-color: var(--accent); }\n"
    ".joy-knob[data-dir=\"stop\"] { color: var(--bad); border-color: var(--bad); }\n"
    ".voice-buttons { display: grid; gap: 0.8rem; }\n"
    ".voice-btn { display: grid; place-items: center; width: 84px; height: 52px; box-sizing: border-box; border-radius: 0.8rem; font-weight: 800; letter-spacing: 0.08em; color: var(--text); border: 2px solid; border-bottom-width: 6px; background: #121c36; transition: transform 80ms, border-bottom-width 80ms, box-shadow 120ms, background 120ms; }\n"
    ".voice-btn.yes { border-color: var(--good); }\n"
    ".voice-btn.no { border-color: var(--bad); }\n"
    ".voice-btn[data-pressed=\"1\"] { transform: translateY(4px); border-bottom-width: 2px; }\n"
    ".voice-btn.yes[data-pressed=\"1\"] { background: rgba(93, 255, 154, 0.25); box-shadow: 0 0 18px rgba(93, 255, 154, 0.55); }\n"
    ".voice-btn.no[data-pressed=\"1\"] { background: rgba(255, 107, 138, 0.25); box-shadow: 0 0 18px rgba(255, 107, 138, 0.55); }\n"
    ".voice-caption { margin-top: 0.5rem; text-align: center; color: var(--muted); font-size: 0.8rem; }\n"
```

Inside the existing `@media (max-width: 720px) {` block add:

```c
    "  .speech-layout { grid-template-columns: 1fr; }\n"
    "  .voice-pad { justify-content: center; }\n"
```

- [ ] **Step 5: Implement JS**

Next to the other speech vars (`var speechThresholdRevision = 0;`) add:

```c
    "  var speechFirstPoll = true;\n"
    "  var voiceTimers = {};\n"
```

Before `function clearSpeech() {` add:

```c
    "  function setJoystick(direction) {\n"
    "    dom.joyKnob.setAttribute('data-dir', direction);\n"
    "    dom.joyKnob.textContent = direction === 'stop' ? 'STOP' : '';\n"
    "    var arrows = document.querySelectorAll('.joy-arrow');\n"
    "    for (var i = 0; i < arrows.length; i++) {\n"
    "      arrows[i].setAttribute('data-active', arrows[i].getAttribute('data-arrow') === direction ? '1' : '0');\n"
    "    }\n"
    "    setText(dom.voiceCaption, 'Mode: ' + direction);\n"
    "  }\n"
    "\n"
    "  function pressVoiceButton(word) {\n"
    "    var button = word === 'yes' ? dom.voiceYes : dom.voiceNo;\n"
    "    button.setAttribute('data-pressed', '1');\n"
    "    window.clearTimeout(voiceTimers[word]);\n"
    "    voiceTimers[word] = window.setTimeout(function () { button.setAttribute('data-pressed', '0'); }, 700);\n"
    "  }\n"
    "\n"
    "  function applyVoiceEvent(label, press) {\n"
    "    if (label === 'up' || label === 'down' || label === 'left' || label === 'right' || label === 'stop') {\n"
    "      setJoystick(label);\n"
    "    } else if (press && (label === 'yes' || label === 'no')) {\n"
    "      pressVoiceButton(label);\n"
    "    }\n"
    "  }\n"
    "\n"
```

In `refreshSpeech`, after `setBadge(dom.speechState, status.state, ...);` add:

```c
    "      dom.voicePad.setAttribute('data-disabled', status.state === 'running' ? '0' : '1');\n"
```

Inside the `if (key !== speechLastEvent) {` block, after `speechLastEvent = key;` add:

```c
    "            applyVoiceEvent(status.last_event.label, !speechFirstPoll);\n"
```

In the `catch (error)` block inside `if (!speechStopped) {` add:

```c
    "        dom.voicePad.setAttribute('data-disabled', '1');\n"
```

In the `finally` block, before the existing poll reschedule line, add:

```c
    "      speechFirstPoll = false;\n"
```

(Must be set only after the response handling, so the very first poll's event is applied with `press=false`.)

In `cacheDom()` after `dom.speechThresholdFeedback = byId('speech-threshold-feedback');` add:

```c
    "    dom.voicePad = byId('voice-pad');\n"
    "    dom.joyKnob = byId('joy-knob');\n"
    "    dom.voiceYes = byId('voice-yes');\n"
    "    dom.voiceNo = byId('voice-no');\n"
    "    dom.voiceCaption = byId('voice-caption');\n"
```

- [ ] **Step 6: Run browser tests**

```bash
NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
for t in apps/zephyr-hello-shell/tests/web/test_*.cjs; do NODE_PATH="$HOME/.copilot/skills/browser/node_modules" node "$t" || echo "FAIL $t"; done
```

Expected: speech test prints the new PASS line; no `FAIL` lines from other web tests. If the default browser path `/usr/bin/microsoft-edge` is missing, set `BROWSER_PATH=/usr/bin/google-chrome`.

- [ ] **Step 7: Visual check**

Take a screenshot of the page served by the test server or the native_sim build and compare with the approved mockup (labels left, joystick + YES/NO right, STOP knob red when centered).

- [ ] **Step 8: Commit**

```bash
git add apps/zephyr-hello-shell/src/web_assets.c apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
git commit -m "feat(web): add voice joystick to speech card" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 4: Docs, board build, flash and live validation

**Files:**
- Modify: `README.md` (Speech AI section: keyword list, six → seven keywords, joystick description, fixture count)
- Modify: `apps/zephyr-hello-shell/third_party/speech-models/README.md` (model classes)
- Modify: `docs/model_debug.md` (add "Direction + stop v3" row with measured float/int8 accuracy and size)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Update docs**

- In `README.md`, replace keyword lists `yes, no, up, down, left, right` with `yes, no, up, down, left, right, stop`; "six keyword fixtures" → "seven keyword fixtures"; add one paragraph: "The Speech AI card shows a voice joystick: up/down/left/right latch the knob, stop centers it, yes/no press the matching button for 0.7 s. Joystick state lives in the browser only."
- In `apps/zephyr-hello-shell/third_party/speech-models/README.md`, update class order to the nine-class list and the training command to include `stop`.
- In `docs/model_debug.md`, add a table row `| Direction + stop v3 | Seven keywords, stock background noise | Float <X>%; int8 <Y>% (N=<N>) |` using the numbers recorded in Task 1 Step 3, and note v3 is the default model.

- [ ] **Step 2: Build and flash the board**

Run: `scripts/flash_zephyr_hello_shell.sh`
Expected: build succeeds with Zephyr SDK toolchain, flash succeeds, validation passes (boot, shell, LEDs, Ethernet, pings).

- [ ] **Step 3: Live check with the user**

Open `http://192.168.55.2/` (default board IP). With the USB microphone on CN13, ask the user to say each of: up, left, down, right, stop, yes, no. Confirm:
- Badge `running`, widget not dimmed.
- Knob follows directions and stays; `stop` centers with red STOP.
- YES/NO press briefly without moving the knob.
Record results in `README.md` measured-results list as a user observation.

- [ ] **Step 4: Commit**

```bash
git add README.md apps/zephyr-hello-shell/third_party/speech-models/README.md docs/model_debug.md
git commit -m "docs(speech): document stop keyword and voice joystick" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
