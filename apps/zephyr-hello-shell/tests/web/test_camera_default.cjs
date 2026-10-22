// Run: node apps/zephyr-hello-shell/tests/web/test_camera_default.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../src/web_assets.c'), 'utf8');
const js = source.split('const uint8_t board_http_app_js[] =')[1].split('\nconst size_t')[0]
  .split('\n')
  .filter((line) => line.trimStart().startsWith('"'))
  .map((line) => JSON.parse(line.trim().replace(/;$/, '')))
  .join('');

for (const readyState of ['loading', 'complete']) {
  const image = {
    removeAttribute(name) { delete this[name]; },
  };
  const frame = { className: 'camera-frame hidden' };
  const toggle = { textContent: 'Show camera' };
  const elements = { 'camera-image': image, 'camera-frame': frame, 'camera-toggle': toggle };
  const documentEvents = {};
  const windowEvents = {};
  const noop = () => {};
  vm.runInNewContext(js, {
    document: {
      readyState,
      getElementById: (id) => elements[id] || null,
      addEventListener: (event, handler) => { documentEvents[event] = handler; },
    },
    window: {
      location: { hostname: '192.0.2.2' },
      addEventListener: (event, handler) => { windowEvents[event] = handler; },
      setInterval: noop,
      setTimeout: noop,
      clearInterval: noop,
      clearTimeout: noop,
      requestAnimationFrame: noop,
    },
    XMLHttpRequest: class {
      open() {}
      setRequestHeader() {}
      send() {}
    },
    fetch: () => new Promise(noop),
    AbortController,
  });
  if (readyState === 'loading') {
    documentEvents.DOMContentLoaded();
  }

  function assertVisible() {
    assert.equal(image.src, 'http://192.0.2.2:8081/');
    assert.equal(frame.className, 'camera-frame');
    assert.equal(toggle.textContent, 'Hide camera');
  }

  function assertHidden() {
    assert.equal(image.src, undefined);
    assert.equal(frame.className, 'camera-frame hidden');
    assert.equal(toggle.textContent, 'Show camera');
  }

  assertVisible();
  toggle.onclick();
  assertHidden();
  toggle.onclick();
  assertVisible();
  windowEvents.pagehide();
  assertHidden();
}
console.log('PASS: camera starts visible, toggles, and disconnects on pagehide');
