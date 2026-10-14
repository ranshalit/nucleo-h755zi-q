// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const path = require('path');
const puppeteer = require('puppeteer-core');
const source = fs.readFileSync(path.join(__dirname, '../../src/web_assets.c'), 'utf8');
function asset(name) {
  const match = source.match(new RegExp('const uint8_t ' + name + '\\[\\] =([\\s\\S]*?);\\n'));
  assert(match);
  return [...match[1].matchAll(/"(?:\\.|[^"\\])*"/g)].map(x => JSON.parse(x[0])).join('');
}
let speech = {state: 'unavailable', reason: 'no ready USB microphone', label: null, score: null,
  last_event: null, threshold_percent: 80};
let failSpeech = false;
let failThreshold = false;
let thresholdRequest = null;
let thresholdRequests = 0;
let ledRequests = 0;
const server = http.createServer((req, res) => {
  res.setHeader('Cache-Control', 'no-store');
  if (req.url === '/') { res.setHeader('Content-Type', 'text/html'); res.end(asset('board_http_index_html')); }
  else if (/\.css$/.test(req.url)) { res.setHeader('Content-Type', 'text/css'); res.end(asset('board_http_styles_css')); }
  else if (/\.js$/.test(req.url)) { res.setHeader('Content-Type', 'text/javascript'); res.end(asset('board_http_app_js')); }
  else {
    res.setHeader('Content-Type', 'application/json');
    if (req.url === '/api/v1/leds') { ledRequests++; }
    if (req.url === '/api/v1/speech/settings' && req.method === 'POST') {
      let body = '';
      req.setEncoding('utf8');
      req.on('data', chunk => { body += chunk; });
      req.on('end', () => {
        thresholdRequest = JSON.parse(body);
        thresholdRequests++;
        if (failThreshold) {
          res.statusCode = 400;
          res.end(JSON.stringify({error: 'threshold rejected'}));
          return;
        }
        speech = {...speech, threshold_percent: thresholdRequest.threshold_percent};
        res.end(JSON.stringify({threshold_percent: speech.threshold_percent}));
      });
    }
    else if (req.url === '/api/v1/speech') { res.statusCode = failSpeech ? 503 : 200; res.end(JSON.stringify(speech)); }
    else if (req.url === '/api/v1/mic') { res.end(JSON.stringify({attached: false, ready: false})); }
    else if (req.url === '/api/v1/mouse') { res.end(JSON.stringify({attached: false})); }
    else { res.end(JSON.stringify({board: {}, network: {}, io: {leds: [], button: {}}})); }
  }
});
async function main() {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const browser = await puppeteer.launch({executablePath: process.env.BROWSER_PATH || '/usr/bin/microsoft-edge',
    headless: false, args: ['--no-sandbox', '--disable-dev-shm-usage']});
  try {
    const page = await browser.newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    const waitText = (id, text) => page.waitForFunction((id, text) =>
      document.getElementById(id)?.textContent === text, {timeout: 5000}, id, text);
    const attr = (id, name) => page.$eval('#' + id, (n, name) => n.getAttribute(name), name);
    const waitAttr = (id, name, value) => page.waitForFunction((id, name, value) =>
      document.getElementById(id)?.getAttribute(name) === value, {timeout: 5000}, id, name, value);
    await waitText('speech-state', 'unavailable');
    await waitAttr('voice-pad', 'data-disabled', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'stop');
    await waitText('speech-threshold-active', 'Active threshold: 80%');
    speech = {...speech, state: 'warming-up', reason: 'collecting audio'};
    await waitText('speech-state', 'warming-up');
    speech = {state: 'running', reason: 'recognizing English yes/no/up/down/left/right/stop', label: 'yes', score: .91,
      last_event: {label: 'yes', sequence: 1, uptime_ms: 12000},
      session: 3, inference_us: 1500, max_processing_us: 2400, windows: 3, gaps: 0, drops: 0,
      threshold_percent: 80};
    await waitText('speech-label', 'yes');
    await waitText('speech-score', 'Model score: 91.0%');
    await waitText('speech-event', 'Detected: yes');
    await waitAttr('voice-pad', 'data-disabled', '0');
    await waitAttr('voice-yes', 'data-pressed', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'stop', 'yes moved joystick');
    await waitAttr('voice-yes', 'data-pressed', '0');
    await page.evaluate(() => {
      window.speechHighlights = 0;
      new MutationObserver((records) => {
        window.speechHighlights += records.filter(r => r.attributeName === 'data-event-sequence').length;
      }).observe(document.getElementById('speech-event'), {attributes: true});
    });
    await new Promise(r => setTimeout(r, 1200));
    assert.equal(await page.evaluate(() => window.speechHighlights), 0, 'same event highlighted twice');
    speech.last_event = {label: 'no', sequence: 2, uptime_ms: 14000};
    speech.label = 'no';
    await waitText('speech-event', 'Detected: no');
    await waitAttr('voice-no', 'data-pressed', '1');
    await waitAttr('voice-no', 'data-pressed', '0');
    assert.equal(await page.evaluate(() => window.speechHighlights), 1);
    speech.last_event = {label: 'left', sequence: 3, uptime_ms: 16000};
    speech.label = 'left';
    await waitText('speech-label', 'left');
    await waitText('speech-event', 'Detected: left');
    assert.equal(await page.evaluate(() => window.speechHighlights), 2);
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
    assert.match(await page.evaluate(() => document.body.textContent), /yes, no, up, down, left, right, stop/);
    await page.$eval('#speech-threshold-input', input => {
      input.value = '65';
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await page.click('#speech-threshold-apply');
    await waitText('speech-threshold-active', 'Active threshold: 65%');
    await waitText('speech-threshold-feedback', 'Threshold updated');
    assert.deepStrictEqual(thresholdRequest, {threshold_percent: 65});
    failThreshold = true;
    await page.$eval('#speech-threshold-input', input => {
      input.value = '70';
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await page.click('#speech-threshold-apply');
    await page.waitForFunction(() =>
      document.getElementById('speech-threshold-feedback')?.textContent.includes('(400)'),
    {timeout: 5000});
    assert.equal(await page.$eval('#speech-threshold-active', node => node.textContent),
      'Active threshold: 65%');
    assert.equal(await page.$eval('#speech-threshold-input', node => node.value), '70');
    const requestsBeforeInvalid = thresholdRequests;
    await page.$eval('#speech-threshold-input', input => {
      input.value = '49';
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await page.click('#speech-threshold-apply');
    await waitText('speech-threshold-feedback', 'Enter a whole number from 50 to 100');
    assert.equal(thresholdRequests, requestsBeforeInvalid, 'invalid local value was sent');
    failThreshold = false;
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
    await waitText('speech-label', '--');
    await waitText('speech-event', 'No accepted keyword');
    await waitAttr('voice-pad', 'data-disabled', '1');
    assert.equal(await attr('joy-knob', 'data-dir'), 'right', 'latched direction lost');
    failSpeech = true;
    await waitText('speech-state', 'error');
    failSpeech = false;
    await waitText('speech-state', 'unavailable');
    speech = {state: 'running', reason: 'recognizing', label: 'silence', score: .9,
      last_event: {label: 'yes', sequence: 9, uptime_ms: 30000}, session: 3,
      inference_us: 1500, max_processing_us: 2400, threshold_percent: 80};
    const page2 = await browser.newPage();
    await page2.goto(`http://127.0.0.1:${server.address().port}`);
    await page2.waitForFunction(() => document.getElementById('voice-pad')?.getAttribute('data-disabled') === '0', {timeout: 5000});
    await new Promise(r => setTimeout(r, 300));
    assert.equal(await page2.$eval('#voice-yes', n => n.getAttribute('data-pressed')), '0', 'stale yes pressed on load');
    await page2.close();
    speech.last_event = {label: 'down', sequence: 10, uptime_ms: 32000};
    const page3 = await browser.newPage();
    await page3.goto(`http://127.0.0.1:${server.address().port}`);
    await page3.waitForFunction(() => document.getElementById('joy-knob')?.getAttribute('data-dir') === 'down', {timeout: 5000});
    speech.last_event = {label: 'no', sequence: 11, uptime_ms: 34000};
    await page3.waitForFunction(() => document.getElementById('voice-no')?.getAttribute('data-pressed') === '1', {timeout: 5000});
    await page3.close();
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
    assert.equal(ledRequests, 0, 'speech changed LED state');
    console.log('PASS: speech states, direction events, joystick/buttons, threshold updates/errors and unchanged LEDs');
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); server.close(); process.exitCode = 1; });
