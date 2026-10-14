# Voice joystick design

## Goal

Show speech keyword events as a joystick on the dashboard. Directions
`up/down/left/right` move the knob; `stop` centers it. `yes`/`no` press
buttons beside the joystick.

## Model: add `stop`

The current v1 model has eight classes: `silence, unknown, yes, no, up, down,
left, right`. It cannot detect `stop`.

- Retrain with `SPEECH_WORDS=yes,no,up,down,left,right,stop
  scripts/train_speech_keywords.sh` (Speech Commands v0.02 has `stop`).
- New class order: `silence, unknown, yes, no, up, down, left, right, stop`.
- Replace `apps/zephyr-hello-shell/models/speech_keywords.tflite`. Keep
  architecture (`tiny_conv`), preprocessing, and int8 input quantization.
  Output grows by one class (about 4 KB of fully connected weights); model
  stays in flash bank 2.
- Firmware: add `SPEECH_STOP` after `SPEECH_RIGHT`, set
  `SPEECH_MODEL_LABELS` to 9, add `"stop"` to JSON labels. `stop` is an
  accepted keyword event like the others.
- Update unit tests, model fixtures, and `docs/model_debug.md` with measured
  float and int8 test accuracy.

## Dashboard (browser-only state)

The joystick is UI-only; firmware keeps no joystick mode.

- The Speech AI card uses two columns: existing labels on the left, controls on
  the right. It stacks on narrow screens.
- Controls: a round joystick base with four direction arrows, a knob, and a
  center STOP marker. YES and NO buttons sit beside it.
- Direction events latch the knob toward that direction and highlight the
  arrow until the next direction or `stop`.
- `stop` centers the knob and highlights STOP.
- `yes`/`no` show their button pressed for 700 ms, then release. They do not
  change the joystick direction.
- Events are applied once, using the existing new-event key
  (`session:sequence:uptime_ms`).
- On page load, the current `last_event` sets the latched direction without
  animation or button press.
- Speech not running: controls show a disabled (dimmed) state; latched
  direction is kept.

## Testing

- Native unit tests: decision and JSON with the `stop` label.
- Model fixture tests updated for nine classes.
- Browser test: event sequence drives knob position, STOP, and YES/NO press;
  repeated poll of the same event does not re-trigger.
- Build, flash, and speak each word on the board; confirm the UI responds.
