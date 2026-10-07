# Dashboard Mouse Trail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw a glowing, smoothly fading route of USB mouse movement in the dashboard mouse pad.

**Architecture:** Browser-only change to the dashboard assets embedded in `web_assets.c`. A `<canvas>` overlay in `#mouse-pad` is redrawn by a `requestAnimationFrame` loop that eases the cursor toward the latest polled target and records eased positions as timestamped trail points drawn as quadratic curves with per-segment alpha.

**Tech Stack:** Zephyr HTTP server serving static C-string HTML/CSS/JS, vanilla ES5 JavaScript, Canvas 2D, Puppeteer (from the local browser skill) for the headless test.

## Global Constraints

- Fade time: 1.5 s, linear alpha `1 - age / 1500 ms`.
- Default color cyan `#63f0ff` (`--accent`); held button colors: left = green `#39ff6a`, right = red `#ff2d2d`, middle = yellow `#ffd60a`; priority left > right > middle.
- Cursor eases toward target by factor 0.35 per animation frame; points appended only when the drawn position moved > 0.5 px.
- No firmware, API, or HTTP server logic changes.
- JS stays ES5 (`var`, no arrow functions) to match the existing asset.

---

### Task 1: Mouse trail canvas, animation loop, test, docs

**Files:**
- Create: `apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs`
- Modify: `apps/zephyr-hello-shell/src/web_assets.c` (HTML `#mouse-pad`, CSS `.mouse-cursor`, JS state/`renderMouse`/`cacheDom`/`startApp`)
- Modify: `README.md` (dashboard mouse paragraph)

**Interfaces:**
- Consumes: existing `renderMouse(mouse)` cumulative `x`/`y`/`buttons`/`session`/`reports` snapshot from `/api/v1/io` `.mouse`.
- Produces: DOM `canvas#mouse-trail.mouse-trail` inside `#mouse-pad`.

- [ ] **Step 1: Write the failing test**

`apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs`:

```js
// Headless check of the dashboard mouse trail.
// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs [http://192.168.55.2/]
const puppeteer = require('puppeteer-core');

const base = process.argv[2] || 'http://192.168.55.2/';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const snap = {
  connected: true, session: 99, reports: 1, buttons: 0,
  x: 0, y: 0, wheel: 0, presses: { left: 0, right: 0, middle: 0 },
};

function fail(msg) { console.error('FAIL: ' + msg); process.exitCode = 1; }

(async () => {
  const browser = await puppeteer.launch({
    executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox'],
  });
  try {
    const page = await browser.newPage();
    await page.setViewport({ width: 1200, height: 900 });
    await page.setRequestInterception(true);
    page.on('request', (req) => {
      if (req.url().endsWith('/api/v1/io')) {
        req.respond({
          status: 200, contentType: 'application/json',
          body: JSON.stringify({ button: { label: 'B1', pressed: false }, leds: [], mouse: snap }),
        });
      } else {
        req.continue();
      }
    });
    await page.goto(base, { waitUntil: 'load' });
    await sleep(500);

    const pixels = () => page.evaluate(() => {
      const c = document.getElementById('mouse-trail');
      if (!c) { return null; }
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      let lit = 0; let cyan = 0; let green = 0;
      for (let i = 0; i < d.length; i += 4) {
        if (d[i + 3] > 40) {
          lit += 1;
          if (d[i + 2] > 150 && d[i] < 150) { cyan += 1; }
          if (d[i + 1] > 150 && d[i] < 150 && d[i + 2] < 150) { green += 1; }
        }
      }
      return { lit, cyan, green };
    });

    if ((await pixels()) === null) { fail('canvas #mouse-trail missing'); return; }

    for (let i = 0; i < 8; i += 1) { snap.x += 12; snap.y += 4; snap.reports += 1; await sleep(100); }
    let p = await pixels();
    console.log('after cyan move:', p);
    if (!(p.lit > 50 && p.cyan > 20)) { fail('expected visible cyan trail'); }

    snap.buttons = 1;
    for (let i = 0; i < 8; i += 1) { snap.y -= 10; snap.reports += 1; await sleep(100); }
    p = await pixels();
    console.log('after left-drag move:', p);
    if (!(p.green > 20)) { fail('expected green trail while left button held'); }

    await sleep(1800);
    p = await pixels();
    console.log('after fade:', p);
    if (p.lit !== 0) { fail('expected trail fully faded after 1.8 s idle'); }

    if (!process.exitCode) { console.log('PASS'); }
  } finally {
    await browser.close();
  }
})();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs`
Expected: `FAIL: canvas #mouse-trail missing` (board runs current firmware).

- [ ] **Step 3: Add canvas to HTML**

In `web_assets.c`, replace:

```c
    "            <span id=\"mouse-cursor\" class=\"mouse-cursor\"></span>\n"
```

with:

```c
    "            <canvas id=\"mouse-trail\" class=\"mouse-trail\"></canvas>\n"
    "            <span id=\"mouse-cursor\" class=\"mouse-cursor\"></span>\n"
```

- [ ] **Step 4: Add CSS**

Replace the `.mouse-cursor` rule's closing lines:

```c
    "  background: var(--accent);\n"
    "  transform: translate(-50%, -50%);\n"
    "}\n"
```

with:

```c
    "  background: var(--accent);\n"
    "  transform: translate(-50%, -50%);\n"
    "  z-index: 1;\n"
    "}\n"
    ".mouse-trail {\n"
    "  position: absolute;\n"
    "  inset: 0;\n"
    "  width: 100%;\n"
    "  height: 100%;\n"
    "  pointer-events: none;\n"
    "}\n"
```

- [ ] **Step 5: Add JS state**

After `"  var ledCards = {};\n"` add:

```c
    "  var trailFadeMs = 1500;\n"
    "  var trailPoints = [];\n"
    "  var trailVisible = false;\n"
    "  var trailCtx = null;\n"
    "  var mouseDrawn = null;\n"
    "  var mouseButtons = 0;\n"
```

- [ ] **Step 6: Make renderMouse set a target instead of moving the cursor**

In `renderMouse`, in the disconnected branch after `"      mousePosition = null;\n"` add:

```c
    "      mouseDrawn = null;\n"
    "      trailPoints = [];\n"
```

Replace the position block:

```c
    "      if (!mousePrevious || mousePrevious.session !== mouse.session ||\n"
    "          mouse.reports < mousePrevious.reports) {\n"
    "        mousePosition = { x: dom.mousePad.clientWidth / 2, y: dom.mousePad.clientHeight / 2 };\n"
    "      } else {\n"
```

with:

```c
    "      if (!mousePrevious || mousePrevious.session !== mouse.session ||\n"
    "          mouse.reports < mousePrevious.reports || !mousePosition) {\n"
    "        mousePosition = { x: dom.mousePad.clientWidth / 2, y: dom.mousePad.clientHeight / 2 };\n"
    "        mouseDrawn = { x: mousePosition.x, y: mousePosition.y };\n"
    "        trailPoints = [];\n"
    "      } else {\n"
```

and replace:

```c
    "      dom.mouseCursor.style.left = mousePosition.x + 'px';\n"
    "      dom.mouseCursor.style.top = mousePosition.y + 'px';\n"
    "    }\n"
    "    mousePrevious = mouse;\n"
```

with:

```c
    "      mouseButtons = buttons;\n"
    "    }\n"
    "    mousePrevious = mouse;\n"
```

- [ ] **Step 7: Add trail drawing + animation functions**

Insert immediately before `"  function markLiveState(label, tone) {\n"`:

```c
    "  function trailColor(buttons) {\n"
    "    if (buttons & 1) {\n"
    "      return '57, 255, 106';\n"
    "    }\n"
    "    if (buttons & 2) {\n"
    "      return '255, 45, 45';\n"
    "    }\n"
    "    if (buttons & 4) {\n"
    "      return '255, 214, 10';\n"
    "    }\n"
    "    return '99, 240, 255';\n"
    "  }\n"
    "\n"
    "  function drawTrail(now) {\n"
    "    var canvas = dom.mouseTrail;\n"
    "    var ratio = window.devicePixelRatio || 1;\n"
    "    var width = canvas.clientWidth;\n"
    "    var height = canvas.clientHeight;\n"
    "    var index;\n"
    "    var p0;\n"
    "    var p1;\n"
    "    var p2;\n"
    "    var alpha;\n"
    "    var color;\n"
    "    if (canvas.width !== Math.round(width * ratio) ||\n"
    "        canvas.height !== Math.round(height * ratio)) {\n"
    "      canvas.width = Math.round(width * ratio);\n"
    "      canvas.height = Math.round(height * ratio);\n"
    "    }\n"
    "    trailCtx.setTransform(ratio, 0, 0, ratio, 0, 0);\n"
    "    trailCtx.clearRect(0, 0, width, height);\n"
    "    while (trailPoints.length && now - trailPoints[0].t >= trailFadeMs) {\n"
    "      trailPoints.shift();\n"
    "    }\n"
    "    trailVisible = trailPoints.length > 1;\n"
    "    trailCtx.lineCap = 'round';\n"
    "    trailCtx.lineJoin = 'round';\n"
    "    trailCtx.lineWidth = 4;\n"
    "    trailCtx.shadowBlur = 10;\n"
    "    for (index = 1; index < trailPoints.length; index += 1) {\n"
    "      p0 = trailPoints[index - 2] || trailPoints[index - 1];\n"
    "      p1 = trailPoints[index - 1];\n"
    "      p2 = trailPoints[index];\n"
    "      alpha = 1 - (now - p2.t) / trailFadeMs;\n"
    "      if (alpha <= 0) {\n"
    "        continue;\n"
    "      }\n"
    "      color = 'rgba(' + trailColor(p2.buttons) + ', ' + alpha.toFixed(3) + ')';\n"
    "      trailCtx.strokeStyle = color;\n"
    "      trailCtx.shadowColor = color;\n"
    "      trailCtx.beginPath();\n"
    "      trailCtx.moveTo((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);\n"
    "      trailCtx.quadraticCurveTo(p1.x, p1.y, (p1.x + p2.x) / 2, (p1.y + p2.y) / 2);\n"
    "      trailCtx.stroke();\n"
    "    }\n"
    "  }\n"
    "\n"
    "  function animateMouse(now) {\n"
    "    var dx;\n"
    "    var dy;\n"
    "    if (mousePosition && mouseDrawn && dom.mouseCursor) {\n"
    "      dx = mousePosition.x - mouseDrawn.x;\n"
    "      dy = mousePosition.y - mouseDrawn.y;\n"
    "      if (Math.abs(dx) > 0.5 || Math.abs(dy) > 0.5) {\n"
    "        mouseDrawn.x += dx * 0.35;\n"
    "        mouseDrawn.y += dy * 0.35;\n"
    "        trailPoints.push({ x: mouseDrawn.x, y: mouseDrawn.y, t: now, buttons: mouseButtons });\n"
    "      } else {\n"
    "        mouseDrawn.x = mousePosition.x;\n"
    "        mouseDrawn.y = mousePosition.y;\n"
    "      }\n"
    "      dom.mouseCursor.style.left = mouseDrawn.x + 'px';\n"
    "      dom.mouseCursor.style.top = mouseDrawn.y + 'px';\n"
    "    }\n"
    "    if (trailCtx && (trailVisible || trailPoints.length)) {\n"
    "      drawTrail(now);\n"
    "    }\n"
    "    window.requestAnimationFrame(animateMouse);\n"
    "  }\n"
    "\n"
```

- [ ] **Step 8: Wire DOM + start the loop**

In `cacheDom` after `"    dom.mouseCursor = byId('mouse-cursor');\n"` add:

```c
    "    dom.mouseTrail = byId('mouse-trail');\n"
```

In `startApp` replace `"    refreshLive();\n"` with:

```c
    "    refreshLive();\n"
    "    if (dom.mouseTrail && dom.mouseTrail.getContext) {\n"
    "      trailCtx = dom.mouseTrail.getContext('2d');\n"
    "    }\n"
    "    window.requestAnimationFrame(animateMouse);\n"
```

- [ ] **Step 9: Update README**

In the README dashboard mouse paragraph, after the sentence ending `...shows a cursor moving within its pad,` change the cursor description to mention the trail: replace `The dashboard's USB mouse card shows a cursor` with `The dashboard's USB mouse card shows a gliding cursor with a glowing trail that fades over about 1.5 s (cyan, or green/red/yellow while left/right/middle is held),`, keeping the rest of the sentence (`moving within its pad, held left/right/middle buttons, ...`).

- [ ] **Step 10: Build, flash, check JS syntax**

Run:
```bash
./scripts/flash_zephyr_hello_shell.sh --skip-validate 2>&1 | grep -E " error|Start operation"
sleep 4; curl -s http://192.168.55.2/app.js > /tmp/app.js && node --check /tmp/app.js && echo JS_OK
```
Expected: `Start operation achieved successfully` and `JS_OK`.

- [ ] **Step 11: Run test to verify it passes**

Run: `NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs`
Expected: three pixel summaries and `PASS`.

- [ ] **Step 12: Commit**

```bash
git add apps/zephyr-hello-shell/src/web_assets.c apps/zephyr-hello-shell/tests/web/test_mouse_trail.cjs README.md docs/superpowers/plans/2026-10-07-mouse-trail.md
git commit -m "feat(web): glowing fading mouse trail"
```
