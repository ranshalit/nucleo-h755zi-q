// Headless check that dashboard stat values glow briefly when they change.
// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_value_glow.cjs [http://192.168.55.2/]
const puppeteer = require('puppeteer-core');

const base = process.argv[2] || 'http://192.168.55.2/';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let celsius = 30.0;

function fail(msg) { console.error('FAIL: ' + msg); process.exitCode = 1; }

(async () => {
  const browser = await puppeteer.launch({
    executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox'],
  });
  try {
    const page = await browser.newPage();
    await page.setRequestInterception(true);
    page.on('request', async (req) => {
      const url = req.url();
      if (url.endsWith('/api/v1/io')) {
        req.respond({
          status: 200, contentType: 'application/json',
          body: JSON.stringify({
            button: { label: 'B1', pressed: false }, leds: [],
            mouse: { connected: false },
            temperature: { available: true, source: 'mcu_die', celsius },
          }),
        });
      } else if (/\/api\/v1\/(dashboard|status)$/.test(url)) {
        // Keep the real payload but pin the temperature so only the test changes it.
        const body = (await (await fetch(url)).text())
          .replace(/"celsius":-?[0-9.]+/g, '"celsius":' + celsius);
        req.respond({ status: 200, contentType: 'application/json', body });
      } else {
        req.continue();
      }
    });
    await page.goto(base, { waitUntil: 'load' });
    await sleep(1500);

    const glowing = (id) => page.$eval('#' + id, (e) => e.parentNode.classList.contains('value-changed'));

    if (await glowing('die-temp')) { fail('first value should not glow'); }

    celsius = 31.5;
    await sleep(300);
    const text = await page.$eval('#die-temp', (e) => e.textContent);
    const shadow = await page.$eval('#die-temp', (e) => getComputedStyle(e.parentNode).boxShadow);
    console.log('after change:', text, await glowing('die-temp'), shadow);
    if (!(await glowing('die-temp'))) { fail('expected glow after temperature change'); }
    if (!shadow || shadow === 'none') { fail('expected visible box-shadow during glow'); }

    await sleep(1200);
    const after = await page.$eval('#die-temp', (e) => getComputedStyle(e.parentNode).boxShadow);
    console.log('after 1.5 s:', after);
    if (after !== 'none' && !/rgba\(99, 240, 255, 0\)/.test(after)) { fail('glow should have faded'); }

    if (await page.$eval('#uptime', (e) => e.getAttribute('data-flash'))) { fail('uptime must not flash'); }

    if (!process.exitCode) { console.log('PASS'); }
  } finally {
    await browser.close();
  }
})();
