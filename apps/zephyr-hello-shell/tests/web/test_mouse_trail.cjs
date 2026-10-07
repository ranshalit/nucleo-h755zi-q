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
    // Headless Chrome may delay requestAnimationFrame right after load; wait until
    // the animation loop has positioned the cursor before generating motion.
    await page.waitForFunction(() => {
      const c = document.getElementById('mouse-cursor');
      return !c || c.style.left !== '';
    }, { timeout: 10000 }).catch(() => {});
    await sleep(200);

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
