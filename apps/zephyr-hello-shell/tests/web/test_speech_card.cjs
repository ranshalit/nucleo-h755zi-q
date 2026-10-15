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
  last_event: null, threshold_percent: 80, accept_frames: 3, rearm_ms: 300, min_gap_ms: 1000};
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
        speech = {...speech, ...thresholdRequest};
        res.end(JSON.stringify({threshold_percent: speech.threshold_percent,
          accept_frames: speech.accept_frames, rearm_ms: speech.rearm_ms, min_gap_ms: speech.min_gap_ms}));
      });
    }
    else if (req.url === '/api/v1/speech') {
      res.statusCode = failSpeech ? 503 : 200;
      res.end(JSON.stringify({session: 0, live_attempt: null, last_attempt: null, ...speech}));
    }
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
    await waitText('speech-timing-active', 'Active acceptance: 3 frames | re-arm 300 ms | gap 1000 ms');
    assert.equal(await page.$eval('#speech-frames-input', n => n.value), '3');
    speech = {...speech, accept_frames: 2, rearm_ms: 200, min_gap_ms: 700};
    await waitText('speech-timing-active', 'Active acceptance: 2 frames | re-arm 200 ms | gap 700 ms');
    assert.equal(await page.$eval('#speech-gap-input', n => n.value), '700');
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
    await waitText('speech-threshold-feedback', 'Settings updated');
    assert.deepStrictEqual(thresholdRequest, {threshold_percent: 65, accept_frames: 2, rearm_ms: 200, min_gap_ms: 700});
    const setInput = (id, value) => page.$eval('#' + id, (input, value) => {
      input.value = value;
      input.dispatchEvent(new Event('input', {bubbles: true}));
    }, value);
    await setInput('speech-frames-input', '3');
    await setInput('speech-rearm-input', '0');
    await setInput('speech-gap-input', '250');
    await page.click('#speech-threshold-apply');
    await waitText('speech-timing-active', 'Active acceptance: 3 frames | re-arm 0 ms | gap 250 ms');
    assert.deepStrictEqual(thresholdRequest, {threshold_percent: 65, accept_frames: 3, rearm_ms: 0, min_gap_ms: 250});
    let before = thresholdRequests;
    for (const [id, bad, msg] of [
      ['speech-frames-input', '0', 'Frames must be a whole number from 1 to 10'],
      ['speech-rearm-input', '2001', 'Re-arm must be a whole number from 0 to 2000 ms'],
      ['speech-gap-input', '-1', 'Gap must be a whole number from 0 to 5000 ms']]) {
      const good = await page.$eval('#' + id, n => n.value);
      await setInput(id, bad);
      await page.click('#speech-threshold-apply');
      await waitText('speech-threshold-feedback', msg);
      await setInput(id, good);
    }
    assert.equal(thresholdRequests, before, 'invalid timing was sent');
    await setInput('speech-frames-input', '1');
    await setInput('speech-rearm-input', '100');
    await setInput('speech-gap-input', '500');
    await page.click('#speech-threshold-apply');
    await waitText('speech-timing-active', 'Active acceptance: 1 frame | re-arm 100 ms | gap 500 ms');
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
    const keywords = ['yes', 'no', 'up', 'down', 'left', 'right', 'stop'];
    const attempt = {
      sequence: 1, start_ms: 100, end_ms: 500, interrupted: false,
      keywords: keywords.map(label => ({
        label, peak: label === 'left' ? .674 : .01, accepted: false,
      })),
    };
    speech = {...speech, state: 'running', label: 'silence', score: .9,
      threshold_percent: 80, live_attempt: null, last_attempt: attempt};
    const waitPeakText = (id, text) => page.waitForFunction((id, text) =>
      document.getElementById(id)?.textContent.includes(text), {timeout: 5000}, id, text);
    await waitPeakText('speech-last-attempt', '67.4%');
    await waitPeakText('speech-last-attempt', '12.6 percentage points below');
    await page.evaluate(() => {
      window.peakAnimations = [];
      const animate = Element.prototype.animate;
      Element.prototype.animate = function (frames, options) {
        if (this.classList.contains('speech-peak-row')) {
          window.peakAnimations.push({word: this.dataset.word, frames});
        }
        return animate.call(this, frames, options);
      };
    });
    speech.live_attempt = {...attempt, sequence: 2, start_ms: 600, end_ms: null};
    await waitPeakText('speech-live-attempt', '67.4%');
    assert.match(await page.$eval('#speech-last-attempt', n => n.textContent), /67.4%/);
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 7);
    await new Promise(r => setTimeout(r, 1200));
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 7, 'same peaks replayed glow');
    speech.live_attempt = {...speech.live_attempt, keywords: speech.live_attempt.keywords.map(e =>
      e.label === 'left' ? {...e, peak: .725} : e)};
    await waitPeakText('speech-live-attempt', '72.5%');
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 8);
    assert.equal(await page.$eval('#speech-live-attempt [data-strongest="1"]', n => n.dataset.word), 'left');
    speech.live_attempt.keywords[4].peak = .805;
    await waitPeakText('speech-live-attempt', 'Above threshold, not accepted');
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 9);
    assert.equal(await attr('joy-knob', 'data-dir'), 'right', 'unaccepted peak changed joystick');
    speech.live_attempt.keywords[4].accepted = true;
    await page.waitForFunction(() =>
      document.querySelector('#speech-live-attempt [data-word="left"]')?.dataset.accepted === '1');
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 9);
    await page.$eval('#speech-threshold-input', input => {
      input.value = '90';
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await page.click('#speech-threshold-apply');
    await waitPeakText('speech-live-attempt', 'Threshold 90%');
    await waitPeakText('speech-live-attempt', '9.5 percentage points below');
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 9, 'threshold change replayed glow');
    await page.$eval('#speech-threshold-input', input => {
      input.value = '50';
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await new Promise(r => setTimeout(r, 700));
    assert.match(await page.$eval('#speech-live-attempt', n => n.textContent), /Threshold 90%/);
    await page.emulateMediaFeatures([{name: 'prefers-reduced-motion', value: 'reduce'}]);
    speech.live_attempt.keywords[4].peak = .9;
    speech.live_attempt.keywords[4].accepted = false;
    await waitPeakText('speech-live-attempt', 'At threshold, not accepted');
    assert.equal(await page.evaluate(() => window.peakAnimations.length), 9, 'reduced motion still animated');
    await page.emulateMediaFeatures([{name: 'prefers-reduced-motion', value: 'no-preference'}]);
    failSpeech = true;
    await waitText('speech-attempt-status', 'Stale peaks: speech status unavailable');
    assert.match(await page.$eval('#speech-live-attempt', n => n.textContent), /90.0%/);
    assert.match(await page.$eval('#speech-last-attempt', n => n.textContent), /67.4%/);
    failSpeech = false;
    const validLive = JSON.parse(JSON.stringify(speech.live_attempt));
    speech.live_attempt = {...validLive, keywords: validLive.keywords.map(e =>
      e.label === 'left' ? {...e, peak: 1.1} : e)};
    await waitText('speech-health', 'Invalid speech attempt response from board');
    assert.match(await page.$eval('#speech-live-attempt', n => n.textContent), /90.0%/);
    assert.doesNotMatch(await page.$eval('#speech-live-attempt', n => n.textContent), /110.0%/);
    speech = {...speech, state: 'warming-up', label: null, score: null,
      live_attempt: null, last_attempt: {...validLive, end_ms: 1100, interrupted: true}};
    await waitPeakText('speech-attempt-status', 'Last attempt interrupted; peaks retained');
    await waitPeakText('speech-attempt-status', 'Historical peaks');
    await waitText('speech-live-attempt', 'No active attempt');
    await waitPeakText('speech-last-attempt', '90.0%');
    speech = {...speech, state: 'running', label: 'unknown', score: .9,
      last_attempt: {...speech.last_attempt, interrupted: false}};
    await waitPeakText('speech-attempt-status', 'Model candidate episodes');
    assert.match(await page.$eval('#speech-last-attempt', n => n.textContent), /90.0%/);
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
    await waitText('speech-last-attempt', 'No completed attempt');
    await waitText('speech-live-attempt', 'No active attempt');
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
    failSpeech = true;
    speech = {state: 'running', reason: 'recognizing', label: 'silence', score: .9,
      last_event: {label: 'no', sequence: 12, uptime_ms: 36000}, session: 3,
      inference_us: 1500, max_processing_us: 2400, threshold_percent: 80};
    const page4 = await browser.newPage();
    await page4.goto(`http://127.0.0.1:${server.address().port}`);
    await page4.waitForFunction(() => document.getElementById('speech-state')?.textContent === 'error', {timeout: 5000});
    failSpeech = false;
    await page4.waitForFunction(() => document.getElementById('voice-pad')?.getAttribute('data-disabled') === '0', {timeout: 5000, polling: 50});
    await new Promise(r => setTimeout(r, 300));
    assert.equal(await page4.$eval('#voice-no', n => n.getAttribute('data-pressed')), '0', 'stale no pressed after failed first poll');
    await page4.close();
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
    assert.equal(ledRequests, 0, 'speech changed LED state');
    console.log('PASS: speech states, events, attempt peaks/glow, silence retention, stale/errors, thresholds and unchanged LEDs');
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); server.close(); process.exitCode = 1; });
