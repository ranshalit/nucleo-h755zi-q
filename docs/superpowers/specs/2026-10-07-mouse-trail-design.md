# Dashboard mouse trail design

## Goal

Show a glowing route of USB mouse movement in the dashboard mouse pad that
fades out smoothly about 1.5 s after it is drawn.

## Decisions

- Fade time: 1.5 s, linear alpha `1 - age / 1500 ms`.
- Color: cyan (`--accent`) by default; while a button is held the segment uses
  the button color: left = green `#39ff6a`, right = red `#ff2d2d`,
  middle = yellow `#ffd60a`. If several are held, priority left > right > middle.
- Smoothing: the visible cursor eases toward the latest target position every
  animation frame; trail points are sampled from the eased position and drawn
  as quadratic curves through segment midpoints.

## Architecture

Browser-only change in `apps/zephyr-hello-shell/src/web_assets.c` (HTML, CSS,
JS). No firmware, API, or HTTP server changes.

- HTML: add `<canvas id="mouse-trail" class="mouse-trail">` inside
  `#mouse-pad`, under `#mouse-cursor`.
- CSS: canvas is absolutely positioned to fill the pad,
  `pointer-events: none`; cursor stays above it.
- JS:
  - `renderMouse()` keeps computing the target position from cumulative
    `x`/`y` deltas (unchanged clamping) but stores it as `mouseTarget` plus the
    current `buttons` instead of moving the cursor directly.
  - A `requestAnimationFrame` loop eases `mouseDrawn` toward `mouseTarget`
    (factor ~0.35 per frame), moves the cursor, appends
    `{x, y, t, buttons}` when the drawn position moved > 0.5 px, drops points
    older than 1500 ms, and redraws the canvas with `globalCompositeOperation`
    default, `shadowBlur` glow, and per-segment alpha.
  - The canvas backing size tracks `clientWidth/Height * devicePixelRatio`.
  - Disconnect, new `session`, or report-counter reset clears trail and
    recentres the cursor.
  - The loop stays cheap when idle: with no points and the cursor at its
    target, it only checks state each frame.

## Error handling

Live I/O request failures keep the existing behavior; the trail simply fades
because no new points arrive.

## Testing

- Headless Chrome against the board, with Puppeteer request interception
  replacing `/api/v1/io` responses by synthetic mouse snapshots: assert
  non-transparent canvas pixels appear, then are fully transparent after
  ~1.6 s.
- Assert a held-left update produces green pixels.
- Manual check on the real board with the Dell MS116 mouse.
- README dashboard paragraph mentions the fading trail.
