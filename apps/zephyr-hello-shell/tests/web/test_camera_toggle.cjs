// Headless check of the dashboard camera Show/Hide toggle.
// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_camera_toggle.cjs [http://192.168.55.2/]
const puppeteer = require('puppeteer-core');
const http = require('http');

const base = process.argv[2] || 'http://192.168.55.2/';
const host = new URL(base).hostname;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function fail(msg) { console.error('FAIL: ' + msg); process.exitCode = 1; }

// Resolves with the number of stream bytes received within `ms` (VLC stand-in).
function streamBytes(ms) {
  return new Promise((resolve) => {
    let bytes = 0;
    const req = http.get({ host, port: 8081, path: '/' }, (res) => {
      res.on('data', (d) => { bytes += d.length; });
    });
    req.on('error', () => {});
    setTimeout(() => { req.destroy(); resolve(bytes); }, ms);
  });
}

(async () => {
  const browser = await puppeteer.launch({
    executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox'],
  });
  try {
    const page = await browser.newPage();
    await page.goto(base, { waitUntil: 'load' });
    await sleep(1000);

    const state = () => page.evaluate(() => {
      const img = document.getElementById('camera-image');
      return {
        hidden: document.getElementById('camera-frame').classList.contains('hidden'),
        src: img.getAttribute('src'),
        width: img.naturalWidth,
        label: document.getElementById('camera-toggle').textContent,
      };
    });

    let s = await state();
    console.log('initial:', s);
    if (!s.hidden || s.src) { fail('camera must be hidden and disconnected by default'); }

    await page.click('#camera-toggle');
    await sleep(2500);
    s = await state();
    console.log('shown:', s);
    if (s.hidden || !(s.width > 0) || s.label !== 'Hide camera') { fail('expected camera frames after Show'); }

    const t0 = Date.now();
    const io = await page.evaluate(() => fetch('/api/v1/io').then((r) => r.status));
    console.log('io while streaming:', io, (Date.now() - t0) + ' ms');
    if (io !== 200) { fail('dashboard API must keep working while streaming'); }

    await page.click('#camera-toggle');
    await sleep(1500);
    s = await state();
    console.log('hidden again:', s);
    if (!s.hidden || s.src) { fail('expected camera hidden and disconnected after Hide'); }

    const bytes = await streamBytes(2500);
    console.log('second client bytes after Hide:', bytes);
    if (!(bytes > 1000)) { fail('stream should be free for another client after Hide'); }

    if (!process.exitCode) { console.log('PASS'); }
  } finally {
    await browser.close();
  }
})();
